#pragma once

#include <Windows.h>
#include <eh.h>
#include <array>
#include <cstdint>
#include <exception>
#include <string_view>
#include <type_traits>
#include <utility>
#include <spdlog/spdlog.h>

namespace Crash
{
	class SEHException final : public std::exception
	{
	public:
		explicit SEHException(unsigned int a_code) noexcept : _code(a_code) {}
		const char* what() const noexcept override { return "Hardware exception while writing crash report"; }
		unsigned int code() const noexcept { return _code; }
	private:
		unsigned int _code;
	};

	inline void seh_translator(unsigned int a_code, EXCEPTION_POINTERS*)
	{
		throw SEHException(a_code);
	}

	inline int section_exception_filter(unsigned long a_code) noexcept
	{
		// C++ exceptions belong to the outer typed catches. Do not try to
		// resume a report after stack exhaustion or a process-corruption fail-fast.
		return a_code == 0xE06D7363u || a_code == EXCEPTION_STACK_OVERFLOW ||
			a_code == 0xC0000374u || a_code == 0xC0000409u ?
			EXCEPTION_CONTINUE_SEARCH : EXCEPTION_EXECUTE_HANDLER;
	}

	__declspec(noinline) inline unsigned long guarded_section(void (*a_function)(void*), void* a_state)
	{
		// /EHa's C++ translator does not catch a breakpoint. The native SEH
		// boundary also covers the secondary DebugBreak observed in OpenComposite.
		__try {
			a_function(a_state);
		} __except (section_exception_filter(GetExceptionCode())) {
			return GetExceptionCode();
		}
		return 0;
	}

	template <class Function>
	void write_section(spdlog::logger& a_log, std::string_view a_name, Function&& a_function)
	{
		a_log.critical("");
		try {
			using Callback = std::remove_reference_t<Function>;
			const auto code = guarded_section([](void* a_state) {
				(*static_cast<Callback*>(a_state))();
			}, std::addressof(a_function));
			if (code) a_log.critical("\t{}: hardware exception 0x{:08X}; continuing report", a_name, code);
		} catch (const SEHException& e) {
			a_log.critical("\t{}: hardware exception 0x{:08X}; continuing report", a_name, e.code());
		} catch (const std::exception& e) {
			a_log.critical("\t{}: {}; continuing report", a_name, e.what());
		} catch (...) {
			a_log.critical("\t{}: unknown exception; continuing report", a_name);
		}
		a_log.flush();
	}

	// Save the supplied fault context before inspecting pointers, loading symbols,
	// or enumerating game objects. Report-generation failures must not replace it.
	inline void write_raw_context(spdlog::logger& a_log, const EXCEPTION_RECORD& a_exception, const CONTEXT& a_context)
	{
		a_log.critical("ORIGINAL EXCEPTION CONTEXT:");
		a_log.critical("\tCode: 0x{:08X}; address: 0x{:016X}; flags: 0x{:08X}",
			a_exception.ExceptionCode, reinterpret_cast<std::uintptr_t>(a_exception.ExceptionAddress), a_exception.ExceptionFlags);
		for (DWORD i = 0; i < a_exception.NumberParameters && i < EXCEPTION_MAXIMUM_PARAMETERS; ++i)
			a_log.critical("\tParameter[{}]: 0x{:016X}", i, a_exception.ExceptionInformation[i]);
		const std::array registers{
			std::pair{"RIP", a_context.Rip}, std::pair{"RSP", a_context.Rsp}, std::pair{"RBP", a_context.Rbp},
			std::pair{"RAX", a_context.Rax}, std::pair{"RBX", a_context.Rbx}, std::pair{"RCX", a_context.Rcx},
			std::pair{"RDX", a_context.Rdx}, std::pair{"RSI", a_context.Rsi}, std::pair{"RDI", a_context.Rdi},
			std::pair{"R8", a_context.R8}, std::pair{"R9", a_context.R9}, std::pair{"R10", a_context.R10},
			std::pair{"R11", a_context.R11}, std::pair{"R12", a_context.R12}, std::pair{"R13", a_context.R13},
			std::pair{"R14", a_context.R14}, std::pair{"R15", a_context.R15}};
		for (const auto& [name, value] : registers) a_log.critical("\t{}: 0x{:016X}", name, value);
		a_log.critical("\tEFLAGS: 0x{:08X}", a_context.EFlags);
		a_log.flush();
	}
}
