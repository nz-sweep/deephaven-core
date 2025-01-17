/*
 * Copyright (c) 2016-2022 Deephaven Data Labs and Patent Pending
 */
#include "deephaven/client/subscription/subscribe_thread.h"

#include <arrow/array.h>
#include <arrow/buffer.h>
#include <arrow/scalar.h>
#include <atomic>
#include <grpc/support/log.h>
#include "deephaven/client/arrowutil/arrow_column_source.h"
#include "deephaven/client/server/server.h"
#include "deephaven/client/utility/arrow_util.h"
#include "deephaven/client/utility/executor.h"
#include "deephaven/dhcore/ticking/ticking.h"
#include "deephaven/dhcore/utility/callbacks.h"
#include "deephaven/dhcore/utility/utility.h"
#include "deephaven/dhcore/ticking/barrage_processor.h"

using deephaven::dhcore::column::ColumnSource;
using deephaven::dhcore::chunk::AnyChunk;
using deephaven::dhcore::clienttable::Schema;
using deephaven::dhcore::ticking::BarrageProcessor;
using deephaven::dhcore::ticking::TickingCallback;
using deephaven::dhcore::utility::Callback;
using deephaven::dhcore::utility::makeReservedVector;
using deephaven::dhcore::utility::separatedList;
using deephaven::dhcore::utility::streamf;
using deephaven::dhcore::utility::stringf;
using deephaven::dhcore::utility::verboseCast;
using deephaven::client::arrowutil::ArrowInt8ColumnSource;
using deephaven::client::arrowutil::ArrowInt16ColumnSource;
using deephaven::client::arrowutil::ArrowInt32ColumnSource;
using deephaven::client::arrowutil::ArrowInt64ColumnSource;
using deephaven::client::arrowutil::ArrowBooleanColumnSource;
using deephaven::client::arrowutil::ArrowDateTimeColumnSource;
using deephaven::client::arrowutil::ArrowStringColumnSource;
using deephaven::client::utility::Executor;
using deephaven::client::utility::okOrThrow;
using deephaven::client::server::Server;
using io::deephaven::proto::backplane::grpc::Ticket;
using arrow::flight::FlightStreamReader;
using arrow::flight::FlightStreamWriter;

namespace deephaven::client::subscription {
namespace {
// This class manages just enough state to serve as a Callback to the ... [TODO: finish this comment]
class SubscribeState final : public Callback<> {
  typedef deephaven::client::server::Server Server;

public:
  SubscribeState(std::shared_ptr<Server> server, std::vector<int8_t> ticketBytes,
      std::shared_ptr<Schema> schema, std::promise<std::shared_ptr<SubscriptionHandle>> promise,
      std::shared_ptr <TickingCallback> callback);
  void invoke() final;

private:
  std::shared_ptr<SubscriptionHandle> invokeHelper();

  std::shared_ptr<Server> server_;
  std::vector<int8_t> ticketBytes_;
  std::shared_ptr<Schema> schema_;
  std::promise<std::shared_ptr<SubscriptionHandle>> promise_;
  std::shared_ptr<TickingCallback> callback_;
};

// The UpdateProcessor class also implements the SubscriptionHandle interface so that our callers
// can cancel us if they want to.
class UpdateProcessor final : public SubscriptionHandle {
public:
  static std::shared_ptr<UpdateProcessor> startThread(std::unique_ptr<FlightStreamReader> fsr,
      std::shared_ptr<Schema> schema, std::shared_ptr<TickingCallback> callback);

  UpdateProcessor(std::unique_ptr<FlightStreamReader> fsr,
      std::shared_ptr<Schema> schema, std::shared_ptr<TickingCallback> callback);
  ~UpdateProcessor() final;

  void cancel() final;

  static void runUntilCancelled(std::shared_ptr<UpdateProcessor> self);
  void runForeverHelper();

private:
  std::unique_ptr<FlightStreamReader> fsr_;
  std::shared_ptr<Schema> schema_;
  std::shared_ptr<TickingCallback> callback_;

  std::mutex mutex_;
  bool cancelled_ = false;
  std::thread thread_;
};

class OwningBuffer final : public arrow::Buffer {
public:
  explicit OwningBuffer(std::vector<uint8_t> data);
  ~OwningBuffer() final;

private:
  std::vector<uint8_t> data_;
};
struct ColumnSourceAndSize {
  std::shared_ptr<ColumnSource> columnSource_;
  size_t size_ = 0;
};

ColumnSourceAndSize arrayToColumnSource(const arrow::Array &array);
}  // namespace

std::shared_ptr<SubscriptionHandle> SubscriptionThread::start(std::shared_ptr<Server> server,
    Executor *flightExecutor, std::shared_ptr<Schema> schema, const Ticket &ticket,
    std::shared_ptr<TickingCallback> callback) {
  std::promise<std::shared_ptr<SubscriptionHandle>> promise;
  auto future = promise.get_future();
  std::vector<int8_t> ticketBytes(ticket.ticket().begin(), ticket.ticket().end());
  auto ss = std::make_shared<SubscribeState>(std::move(server), std::move(ticketBytes),
      std::move(schema), std::move(promise), std::move(callback));
  flightExecutor->invoke(std::move(ss));
  return future.get();
}

namespace {
SubscribeState::SubscribeState(std::shared_ptr<Server> server, std::vector<int8_t> ticketBytes,
    std::shared_ptr<Schema> schema,
    std::promise<std::shared_ptr<SubscriptionHandle>> promise,
    std::shared_ptr<TickingCallback> callback) :
    server_(std::move(server)), ticketBytes_(std::move(ticketBytes)), schema_(std::move(schema)),
    promise_(std::move(promise)), callback_(std::move(callback)) {}

void SubscribeState::invoke() {
  try {
    auto subscriptionHandle = invokeHelper();
    // If you made it this far, then you have been successful!
    promise_.set_value(std::move(subscriptionHandle));
  } catch (const std::exception &e) {
    promise_.set_exception(std::make_exception_ptr(e));
  }
}

std::shared_ptr<SubscriptionHandle> SubscribeState::invokeHelper() {
  arrow::flight::FlightCallOptions fco;
  server_->forEachHeaderNameAndValue(
    [&fco](const std::string &name, const std::string &value) {
      fco.headers.emplace_back(name, value);
    }
  );
  auto *client = server_->flightClient();

  arrow::flight::FlightDescriptor descriptor;
  char magicData[4];
  auto src = BarrageProcessor::deephavenMagicNumber;
  static_assert(sizeof(src) == sizeof(magicData));
  memcpy(magicData, &src, sizeof(magicData));

  descriptor.type = arrow::flight::FlightDescriptor::DescriptorType::CMD;
  descriptor.cmd = std::string(magicData, 4);
  std::unique_ptr<FlightStreamWriter> fsw;
  std::unique_ptr<FlightStreamReader> fsr;
  okOrThrow(DEEPHAVEN_EXPR_MSG(client->DoExchange(fco, descriptor, &fsw, &fsr)));

  auto subReqRaw = BarrageProcessor::createSubscriptionRequest(ticketBytes_.data(), ticketBytes_.size());
  auto buffer = std::make_shared<OwningBuffer>(std::move(subReqRaw));
  okOrThrow(DEEPHAVEN_EXPR_MSG(fsw->WriteMetadata(std::move(buffer))));

  // Run forever (until error or cancellation)
  auto processor = UpdateProcessor::startThread(std::move(fsr), std::move(schema_),
      std::move(callback_));
  return processor;
}

std::shared_ptr<UpdateProcessor> UpdateProcessor::startThread(
    std::unique_ptr<FlightStreamReader> fsr, std::shared_ptr<Schema> schema,
    std::shared_ptr<TickingCallback> callback) {
  auto result = std::make_shared<UpdateProcessor>(std::move(fsr), std::move(schema),
      std::move(callback));
  result->thread_ = std::thread(&runUntilCancelled, result);
  return result;
}

UpdateProcessor::UpdateProcessor(std::unique_ptr<FlightStreamReader> fsr,
    std::shared_ptr<Schema> schema, std::shared_ptr<TickingCallback> callback) :
    fsr_(std::move(fsr)), schema_(std::move(schema)), callback_(std::move(callback)),
    cancelled_(false) {}

UpdateProcessor::~UpdateProcessor() {
  cancel();
}

void UpdateProcessor::cancel() {
  static const char *const me = "UpdateProcessor::cancel";
  gpr_log(GPR_INFO, "%s: Subscription shutdown requested.", me);
  std::unique_lock guard(mutex_);
  if (cancelled_) {
    guard.unlock(); // to be nice
    gpr_log(GPR_ERROR, "%s: Already cancelled.", me);
    return;
  }
  cancelled_ = true;
  guard.unlock();

  fsr_->Cancel();
  thread_.join();
}

void UpdateProcessor::runUntilCancelled(std::shared_ptr<UpdateProcessor> self) {
  try {
    self->runForeverHelper();
  } catch (...) {
    // If the thread was been cancelled via explicit user action, then swallow all errors.
    if (!self->cancelled_) {
      self->callback_->onFailure(std::current_exception());
    }
  }
}

void UpdateProcessor::runForeverHelper() {
  // Reuse the chunk for efficiency.
  arrow::flight::FlightStreamChunk flightStreamChunk;
  BarrageProcessor bp(schema_);
  // Process Arrow Flight messages until error or cancellation.
  while (true) {
    okOrThrow(DEEPHAVEN_EXPR_MSG(fsr_->Next(&flightStreamChunk)));
    const auto &cols = flightStreamChunk.data->columns();
    auto columnSources = makeReservedVector<std::shared_ptr<ColumnSource>>(cols.size());
    auto sizes = makeReservedVector<size_t>(cols.size());
    for (const auto &col : cols) {
      auto css = arrayToColumnSource(*col);
      columnSources.push_back(std::move(css.columnSource_));
      sizes.push_back(css.size_);
    }

    const void *metadata = nullptr;
    size_t metadataSize = 0;
    if (flightStreamChunk.app_metadata != nullptr) {
      metadata = flightStreamChunk.app_metadata->data();
      metadataSize = flightStreamChunk.app_metadata->size();
    }
    auto result = bp.processNextChunk(columnSources, sizes, metadata, metadataSize);

    if (result.has_value()) {
      callback_->onTick(std::move(*result));
    }
  }
}

OwningBuffer::OwningBuffer(std::vector<uint8_t> data) :
    arrow::Buffer(data.data(), (int64_t)data.size()), data_(std::move(data)) {}
OwningBuffer::~OwningBuffer() = default;

struct ArrayToColumnSourceVisitor final : public arrow::ArrayVisitor {
  explicit ArrayToColumnSourceVisitor(std::shared_ptr<arrow::Array> storage) : storage_(std::move(storage)) {}

  arrow::Status Visit(const arrow::Int8Array &array) final {
    result_ = ArrowInt8ColumnSource::create(std::move(storage_), &array);
    return arrow::Status::OK();
  }

  arrow::Status Visit(const arrow::Int16Array &array) final {
    result_ = ArrowInt16ColumnSource::create(std::move(storage_), &array);
    return arrow::Status::OK();
  }

  arrow::Status Visit(const arrow::Int32Array &array) final {
    result_ = ArrowInt32ColumnSource::create(std::move(storage_), &array);
    return arrow::Status::OK();
  }

  arrow::Status Visit(const arrow::Int64Array &array) final {
    result_ = ArrowInt64ColumnSource::create(std::move(storage_), &array);
    return arrow::Status::OK();
  }

  arrow::Status Visit(const arrow::BooleanArray &array) final {
    result_ = ArrowBooleanColumnSource::create(std::move(storage_), &array);
    return arrow::Status::OK();
  }

  arrow::Status Visit(const arrow::StringArray &array) final {
    result_ = ArrowStringColumnSource::create(std::move(storage_), &array);
    return arrow::Status::OK();
  }

  arrow::Status Visit(const arrow::TimestampArray &array) final {
    result_ = ArrowDateTimeColumnSource::create(std::move(storage_), &array);
    return arrow::Status::OK();
  }

  // We keep a shared_ptr to the arrow array in order to keep the underlying storage alive.
  std::shared_ptr<arrow::Array> storage_;
  std::shared_ptr<ColumnSource> result_;
};

// Creates a non-owning chunk of the right type that points to the corresponding array data.
ColumnSourceAndSize arrayToColumnSource(const arrow::Array &array) {
  const auto *listArray = verboseCast<const arrow::ListArray*>(DEEPHAVEN_EXPR_MSG(&array));

  if (listArray->length() != 1) {
    auto message = stringf("Expected array of length 1, got %o", array.length());
    throw std::runtime_error(DEEPHAVEN_DEBUG_MSG(message));
  }

  const auto listElement = listArray->GetScalar(0).ValueOrDie();
  const auto *listScalar = verboseCast<const arrow::ListScalar*>(DEEPHAVEN_EXPR_MSG(listElement.get()));
  const auto &listScalarValue = listScalar->value;

  ArrayToColumnSourceVisitor v(listScalarValue);
  okOrThrow(DEEPHAVEN_EXPR_MSG(listScalarValue->Accept(&v)));
  return {std::move(v.result_), size_t(listScalarValue->length())};
}
}  // namespace
}  // namespace deephaven::client::subscription
