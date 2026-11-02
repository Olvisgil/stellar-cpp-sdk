// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0
#pragma once
/// @file clock.hpp
/// Injectable Clock/RandomSource/Logger — deterministic seams for tests and
/// for logging that redacts secrets before they reach sink backends.
#include <chrono>
#include <string>
#include <string_view>
#include "stellar/export.hpp"
#include "stellar/util/bytes.hpp"
namespace stellar::inline v1 {

/// Monotonic + wall clock. Inject into builders/clients that stamp
/// timebounds, nonces or TTLs so tests control time.
class STELLAR_API Clock {
 public:
  virtual ~Clock() = default;
  /// Current wall time (unix seconds).
  [[nodiscard]] virtual std::int64_t now_seconds() const = 0;
  /// Monotonic seconds (arbitrary epoch) for elapsed-time maths.
  [[nodiscard]] virtual std::int64_t monotonic_seconds() const = 0;
};

/// The default system-backed clock.
class STELLAR_API SystemClock final : public Clock {
 public:
  [[nodiscard]] std::int64_t now_seconds() const override {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
  }
  [[nodiscard]] std::int64_t monotonic_seconds() const override {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
  }
};

/// Deterministic clock: monotonic starts at 0 and only advances via
/// `advance`; wall time is a fixed offset — tests drive it explicitly.
class STELLAR_API FakeClock final : public Clock {
 public:
  explicit FakeClock(std::int64_t now = 0) : now_(now) {}
  [[nodiscard]] std::int64_t now_seconds() const override { return now_; }
  [[nodiscard]] std::int64_t monotonic_seconds() const override { return mono_; }
  void advance(std::int64_t s) {
    now_ += s;
    mono_ += s;
  }
  void set_now(std::int64_t s) { now_ = s; }

 private:
  std::int64_t now_;
  std::int64_t mono_{0};
};

/// Cryptographically-random byte source. Inject into nonce generation.
class STELLAR_API RandomSource {
 public:
  virtual ~RandomSource() = default;
  [[nodiscard]] virtual Bytes bytes(std::size_t n) = 0;
};

/// Default: libsodium-backed RNG (constant-time, seeded once).
class STELLAR_API SecureRandom final : public RandomSource {
 public:
  [[nodiscard]] Bytes bytes(std::size_t n) override;
};

/// Deterministic RNG for tests (reproducible byte streams).
class STELLAR_API DeterministicRandom final : public RandomSource {
 public:
  explicit DeterministicRandom(std::uint8_t seed = 0) : next_(seed) {}
  [[nodiscard]] Bytes bytes(std::size_t n) override {
    Bytes out(n);
    for (auto& b : out) b = next_++;
    return out;
  }

 private:
  std::uint8_t next_;
};

/// Severity levels.
enum class LogLevel { kTrace, kDebug, kInfo, kWarn, kError };

/// Logging sink: implementations write formatted lines. Callers must never
/// pass secrets — redact() is applied to every message first.
class STELLAR_API Logger {
 public:
  virtual ~Logger() = default;
  virtual void log(LogLevel level, std::string_view message) = 0;
  [[nodiscard]] virtual LogLevel min_level() const noexcept { return LogLevel::kTrace; }
};

/// Default: discards everything (opt-in logging).
class STELLAR_API NullLogger final : public Logger {
 public:
  void log(LogLevel, std::string_view) override {}
};

/// stderr-backed logger.
class STELLAR_API StderrLogger final : public Logger {
 public:
  explicit StderrLogger(LogLevel min = LogLevel::kInfo) : min_(min) {}
  void log(LogLevel level, std::string_view message) override;
  [[nodiscard]] LogLevel min_level() const noexcept override { return min_; }

 private:
  LogLevel min_;
};

/// Redact secrets (hex seeds, JWTs, auth headers) from a message before
/// logging. Applied by every call site — never log raw keys.
[[nodiscard]] std::string redact(std::string_view message);

}  // namespace stellar::inline v1
