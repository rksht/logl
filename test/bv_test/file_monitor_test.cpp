#include <learnogl/file_monitor.h>
#include <learnogl/eng>

#if __has_include(<unistd.h>)
#include <unistd.h>
#define SLEEP(N) sleep(N)
#else
#include <windows.h>
#define SLEEP(N) Sleep(N * 1000)
#endif

#include <iostream>

using namespace fo;

// Something to do in this midst of polling file changes
u64 factorial(u64 n) {
	u64 p = 1;
	for (u64 i = 1; i <= n; ++i) {
		p = p * i;
	}
	return p;
}

int main() {
	memory_globals::init();
	DEFER([]() { memory_globals::shutdown(); });

	eng::init_non_gl_globals();
	DEFER([]() { eng::close_non_gl_globals(); });
	{
		FileMonitor fm;

#ifdef _MSC_VER
		std::array<const char *, 3> pathnames{"E:/build/a/b/FILE1.txt", "E:/build/a/c/d/FILE2.txt",
											  "E:/build/a/c/d/FILE3.txt"};
		// std::array<const char *, 3> pathnames{"E:\\build\\a\\b\\FILE1.txt",
		// "E:\\build\\a\\c\\d\\FILE2.txt", "E:\\build\\a\\c\\d\\FILE3.txt"};
#else
		std::array<const char *, 4> pathnames{"/tmp/file1", "/tmp/file2", "/tmp/file3", "/tmp/file4"};
#endif

		for (size_t i = 0; i < pathnames.size(); ++i) {
			fm.add_listener(pathnames[i], [&pathnames, i](FileMonitor::ListenerArgs args) -> void {
				LOG_F(INFO, "Triggered - event type = %u, for fule %s", static_cast<u32>(args.event_type),
					  pathnames[i]);
			});
		}

		u64 n = 0;
		while (true) {
			// SLEEP(1);
			u32 count = fm.poll_changes();
			if (count > 0) {
				std::cout << "Polled change" << std::endl;
				sleep(1);
			}
			LOG_F(INFO, "Listeners called = %u", count);
			std::cout << "Factorial of " << n << " = " << factorial(n) << std::endl;
			++n;
		}
	}
}
