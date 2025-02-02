/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file crashlog_unix.cpp Unix crash log handler */

#include "../../stdafx.h"
#include "../../crashlog.h"
#include "../../fileio_func.h"
#include "../../string_func.h"
#include "../../gamelog.h"
#include "../../saveload/saveload.h"

#include <signal.h>
#include <sys/utsname.h>

#if defined(__GLIBC__)
/* Execinfo (and thus making stacktraces) is a GNU extension */
#	include <execinfo.h>
#endif

#if defined(__NetBSD__)
#include <unistd.h>
#endif

#ifdef WITH_UNOFFICIAL_BREAKPAD
#	include <client/linux/handler/exception_handler.h>
#endif

#include "../../safeguards.h"

/**
 * Unix implementation for the crash logger.
 */
class CrashLogUnix : public CrashLog {
	/** Signal that has been thrown. */
	int signum;

	void LogOSVersion(std::back_insert_iterator<std::string> &output_iterator) const override
	{
		struct utsname name;
		if (uname(&name) < 0) {
			 fmt::format_to(output_iterator, "Could not get OS version: {}\n", strerror(errno));
			 return;
		}

		fmt::format_to(output_iterator,
				"Operating system:\n"
				" Name:     {}\n"
				" Release:  {}\n"
				" Version:  {}\n"
				" Machine:  {}\n",
				name.sysname,
				name.release,
				name.version,
				name.machine
		);
	}

	void LogError(std::back_insert_iterator<std::string> &output_iterator, const std::string_view message) const override
	{
		fmt::format_to(output_iterator,
			   "Crash reason:\n"
				" Signal:  {} ({})\n"
				" Message: {}\n\n",
				strsignal(this->signum),
				this->signum,
				message
		);
	}

	void LogStacktrace(std::back_insert_iterator<std::string> &output_iterator) const override
	{
		fmt::format_to(output_iterator, "Stacktrace:\n");
#if defined(__GLIBC__)
		void *trace[64];
		int trace_size = backtrace(trace, lengthof(trace));

		char **messages = backtrace_symbols(trace, trace_size);
		for (int i = 0; i < trace_size; i++) {
			fmt::format_to(output_iterator, " [{:02}] {}\n", i, messages[i]);
		}
		free(messages);
#else
		fmt::format_to(output_iterator, " Not supported.\n");
#endif
		fmt::format_to(output_iterator, "\n");
	}

#ifdef WITH_UNOFFICIAL_BREAKPAD
	static bool MinidumpCallback(const google_breakpad::MinidumpDescriptor& descriptor, void* context, bool succeeded)
	{
		CrashLogUnix *crashlog = reinterpret_cast<CrashLogUnix *>(context);

		crashlog->crashdump_filename = crashlog->CreateFileName(".dmp");
		std::rename(descriptor.path(), crashlog->crashdump_filename.c_str());
		return succeeded;
	}

	int WriteCrashDump() override
	{
		return google_breakpad::ExceptionHandler::WriteMinidump(_personal_dir, MinidumpCallback, this) ? 1 : -1;
	}
#endif

public:
	/**
	 * A crash log is always generated by signal.
	 * @param signum the signal that was caused by the crash.
	 */
	CrashLogUnix(int signum) :
		signum(signum)
	{
	}
};

/** The signals we want our crash handler to handle. */
static const int _signals_to_handle[] = { SIGSEGV, SIGABRT, SIGFPE, SIGBUS, SIGILL };

/**
 * Entry point for the crash handler.
 * @note Not static so it shows up in the backtrace.
 * @param signum the signal that caused us to crash.
 */
static void CDECL HandleCrash(int signum)
{
	/* Disable all handling of signals by us, so we don't go into infinite loops. */
	for (const int *i = _signals_to_handle; i != endof(_signals_to_handle); i++) {
		signal(*i, SIG_DFL);
	}

	if (_gamelog.TestEmergency()) {
		fmt::print("A serious fault condition occurred in the game. The game will shut down.\n");
		fmt::print("As you loaded an emergency savegame no crash information will be generated.\n");
		abort();
	}

	if (SaveloadCrashWithMissingNewGRFs()) {
		fmt::print("A serious fault condition occurred in the game. The game will shut down.\n");
		fmt::print("As you loaded an savegame for which you do not have the required NewGRFs\n");
		fmt::print("no crash information will be generated.\n");
		abort();
	}

	CrashLogUnix log(signum);
	log.MakeCrashLog();

	CrashLog::AfterCrashLogCleanup();
	abort();
}

/* static */ void CrashLog::InitialiseCrashLog()
{
	for (const int *i = _signals_to_handle; i != endof(_signals_to_handle); i++) {
		signal(*i, HandleCrash);
	}
}

/* static */ void CrashLog::InitThread()
{
}
