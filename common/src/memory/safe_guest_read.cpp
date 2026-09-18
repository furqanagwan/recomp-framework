#include "recomp/memory/safe_guest_read.h"

#include <cstdint>
#include <limits>

#if defined(_WIN32)
#include <Windows.h>
#else
#include <mutex>
#include <setjmp.h>
#include <signal.h>
#include <unistd.h>
#endif

namespace recomp::memory {
namespace {

void CopyVolatileBytes(const void* source, void* destination, size_t size) noexcept {
  auto* source_bytes = static_cast<const volatile uint8_t*>(source);
  auto* destination_bytes = static_cast<uint8_t*>(destination);
  for (size_t i = 0; i < size; ++i) {
    destination_bytes[i] = source_bytes[i];
  }
}

#if defined(_WIN32)

int SafeReadExceptionFilter(unsigned long exception_code, EXCEPTION_POINTERS* exception,
                            uintptr_t source_begin, uintptr_t source_end) noexcept {
  if (exception_code != EXCEPTION_ACCESS_VIOLATION && exception_code != EXCEPTION_IN_PAGE_ERROR) {
    return EXCEPTION_CONTINUE_SEARCH;
  }
  const EXCEPTION_RECORD* record = exception == nullptr ? nullptr : exception->ExceptionRecord;
  if (record == nullptr || record->NumberParameters < 2 || record->ExceptionInformation[0] != 0) {
    return EXCEPTION_CONTINUE_SEARCH;
  }
  const uintptr_t fault_address = record->ExceptionInformation[1];
  return fault_address >= source_begin && fault_address < source_end ? EXCEPTION_EXECUTE_HANDLER
                                                                     : EXCEPTION_CONTINUE_SEARCH;
}

#else

struct SafeReadContext {
  sigjmp_buf jump_buffer;
  uintptr_t source_begin = 0;
  uintptr_t source_end = 0;
  SafeReadContext* previous = nullptr;
};

thread_local SafeReadContext* volatile active_context = nullptr;
std::once_flag handlers_once;
bool handlers_installed = false;
struct sigaction previous_sigsegv{};
struct sigaction previous_sigbus{};

const struct sigaction& PreviousAction(int signal_number) noexcept {
  return signal_number == SIGBUS ? previous_sigbus : previous_sigsegv;
}

[[noreturn]] void RaiseDefault(int signal_number) noexcept {
  struct sigaction default_action{};
  default_action.sa_handler = SIG_DFL;
  sigemptyset(&default_action.sa_mask);
  sigaction(signal_number, &default_action, nullptr);
  kill(getpid(), signal_number);
  _exit(128 + signal_number);
}

void ForwardSignal(int signal_number, siginfo_t* info, void* platform_context) noexcept {
  const struct sigaction& previous = PreviousAction(signal_number);
  if (previous.sa_handler == SIG_DFL || previous.sa_handler == SIG_IGN) {
    RaiseDefault(signal_number);
  }
  if ((previous.sa_flags & SA_SIGINFO) != 0) {
    previous.sa_sigaction(signal_number, info, platform_context);
  } else {
    previous.sa_handler(signal_number);
  }
}

void SafeReadSignalHandler(int signal_number, siginfo_t* info, void* platform_context) noexcept {
  SafeReadContext* context = active_context;
  const uintptr_t fault_address = info == nullptr ? 0 : reinterpret_cast<uintptr_t>(info->si_addr);
  if (context != nullptr && fault_address >= context->source_begin &&
      fault_address < context->source_end) {
    active_context = context->previous;
    siglongjmp(context->jump_buffer, 1);
  }
  ForwardSignal(signal_number, info, platform_context);
}

bool IsSafeReadHandler(const struct sigaction& action) noexcept {
  return (action.sa_flags & SA_SIGINFO) != 0 && action.sa_sigaction == SafeReadSignalHandler;
}

bool EnsureSignalHandler(int signal_number, struct sigaction& previous) {
  struct sigaction current{};
  if (sigaction(signal_number, nullptr, &current) != 0) {
    return false;
  }
  if (IsSafeReadHandler(current)) {
    return true;
  }

  previous = current;
  struct sigaction replacement{};
  replacement.sa_sigaction = SafeReadSignalHandler;
  sigemptyset(&replacement.sa_mask);
  sigaddset(&replacement.sa_mask, SIGSEGV);
  sigaddset(&replacement.sa_mask, SIGBUS);
  replacement.sa_flags = SA_SIGINFO;
  return sigaction(signal_number, &replacement, nullptr) == 0;
}

void InstallSignalHandlers() {
  handlers_installed = EnsureSignalHandler(SIGSEGV, previous_sigsegv) &&
                       EnsureSignalHandler(SIGBUS, previous_sigbus);
}

bool EnsureSignalHandlers() {
  std::call_once(handlers_once, InstallSignalHandlers);
  return handlers_installed;
}

#endif

}  // namespace

bool TryReadGuestMemory(const void* guest_host_address, void* destination, size_t size) noexcept {
  if (size == 0) {
    return true;
  }
  if (guest_host_address == nullptr || destination == nullptr) {
    return false;
  }

  const uintptr_t source_begin = reinterpret_cast<uintptr_t>(guest_host_address);
  if (size > std::numeric_limits<uintptr_t>::max() - source_begin) {
    return false;
  }

#if defined(_WIN32)
  __try {
    CopyVolatileBytes(guest_host_address, destination, size);
    return true;
  } __except (SafeReadExceptionFilter(GetExceptionCode(), GetExceptionInformation(), source_begin,
                                      source_begin + size)) {
    return false;
  }
#else
  if (!EnsureSignalHandlers()) {
    return false;
  }
  SafeReadContext context;
  context.source_begin = source_begin;
  context.source_end = source_begin + size;
  context.previous = active_context;
  if (sigsetjmp(context.jump_buffer, 1) != 0) {
    active_context = context.previous;
    return false;
  }

  active_context = &context;
  CopyVolatileBytes(guest_host_address, destination, size);
  active_context = context.previous;
  return true;
#endif
}

}  // namespace recomp::memory
