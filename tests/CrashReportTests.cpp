#include "Crash/ReportWriter.h"
#include <spdlog/sinks/basic_file_sink.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
	void require(bool value, const char* message)
	{
		if (!value) throw std::runtime_error(message);
	}

	__declspec(noinline) void invalidRead()
	{
		const auto* memory = reinterpret_cast<volatile const int*>(1);
		const auto value = *memory;
		(void)value;
	}
}

int main(int argc, char** argv)
{
	if (argc != 2) return 2;
	const auto previous = _set_se_translator(Crash::seh_translator);
	try {
		const auto directory = std::filesystem::absolute(argv[1]);
		std::filesystem::create_directories(directory);
		const auto path = directory / "crash-report-regression.log";
		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_st>(path.string(), true);
		spdlog::logger log("regression", sink);
		log.set_pattern("%v");
		EXCEPTION_RECORD exception{};
		exception.ExceptionCode = EXCEPTION_ACCESS_VIOLATION;
		exception.ExceptionAddress = reinterpret_cast<void*>(0x140DBDE4Eull);
		exception.NumberParameters = 2;
		exception.ExceptionInformation[0] = 0;
		exception.ExceptionInformation[1] = 0x1B0;
		CONTEXT context{};
		context.Rip = 0x140DBDE4Eull;
		context.Rsp = 0x12345678;
		context.R15 = 0xABCDEF;
		context.EFlags = 0x202;
		Crash::write_raw_context(log, exception, context);
		const auto read = [&]() {
			std::ifstream file(path);
			return std::string(std::istreambuf_iterator<char>(file), {});
		};
		const auto original = read();
		require(original.find("Code: 0xC0000005") != std::string::npos &&
			original.find("RIP: 0x0000000140DBDE4E") != std::string::npos &&
			original.find("RSP: 0x0000000012345678") != std::string::npos &&
			original.find("R15: 0x0000000000ABCDEF") != std::string::npos &&
			original.find("Parameter[1]: 0x00000000000001B0") != std::string::npos,
			"original fault was not flushed before enrichment");
		Crash::write_section(log, "symbol lookup", [] { invalidRead(); });
		Crash::write_section(log, "optional metadata", [] { RaiseException(EXCEPTION_BREAKPOINT, 0, 0, nullptr); });
		Crash::write_section(log, "C++ metadata", [] { throw std::runtime_error("fixture metadata failure"); });
		Crash::write_section(log, "modules", [&] { log.critical("MODULES: openvr_api.dll; Buffout4.dll"); });
		Crash::write_section(log, "completion", [&] { log.critical("REPORT COMPLETE"); });
		const auto completed = read();
		require(completed.starts_with(original), "a reporting error erased the original fault");
		require(completed.find("symbol lookup: hardware exception 0xC0000005") != std::string::npos,
			"access violation during reporting was not isolated");
		require(completed.find("optional metadata: hardware exception 0x80000003") != std::string::npos,
			"secondary breakpoint during reporting was not isolated");
		require(completed.find("fixture metadata failure") != std::string::npos &&
			completed.find("MODULES: openvr_api.dll; Buffout4.dll") != std::string::npos &&
			completed.find("REPORT COMPLETE") != std::string::npos,
			"report stopped after a section failure");
		_set_se_translator(previous);
		std::cout << "Original fault preserved; real access violation, breakpoint and C++ section failures isolated; later sections flushed.\n";
		return 0;
	} catch (const std::exception& e) {
		_set_se_translator(previous);
		std::cerr << e.what() << '\n';
		return 1;
	}
}
