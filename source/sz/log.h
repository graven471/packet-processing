#pragma once

#include <errno.h>
#include <sys/stat.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LOG_ERROR(fmt, ...) \
	fprintf(stderr, "[ERROR] %s:%d (%s): " fmt "\n", __FILE__, __LINE__, __func__ __VA_OPT__(, ) __VA_ARGS__)

#define LOG_WARN(fmt, ...) \
	printf("[WARNING] %s:%d (%s): " fmt "\n", __FILE__, __LINE__, __func__ __VA_OPT__(, ) __VA_ARGS__)

#define CRASH_DIR "crashdump"

#define LOG_FATAL(fmt, ...)                                                           \
	do {                                                                          \
		/* ensure crashdump/ exists */                                        \
		if (mkdir(CRASH_DIR, 0755) != 0 && errno != EEXIST) {                 \
			LOG_ERROR("cannot create crash dir");                         \
			abort();                                                      \
		}                                                                     \
                                                                                      \
		/* build timestamped filename */                                      \
		time_t _t = time(NULL);                                               \
		struct tm _tm;                                                        \
		localtime_r(&_t, &_tm);                                               \
		char _path[256];                                                      \
		char _stamp[64];                                                      \
		strftime(_stamp, sizeof(_stamp), "%Y-%m-%d_%H-%M-%S", &_tm);          \
		snprintf(_path, sizeof(_path), "%s/crash_%s.log", CRASH_DIR, _stamp); \
                                                                                      \
		/* write crash report */                                              \
		FILE *_f = fopen(_path, "w");                                         \
		if (!_f)                                                              \
			_f = stderr;                                                  \
                                                                                      \
		fprintf(_f, "=== CRASH REPORT ===\n");                                \
		fprintf(_f, "Time:     %s\n", _stamp);                                \
		fprintf(_f, "File:     %s\n", __FILE__);                              \
		fprintf(_f, "Line:     %d\n", __LINE__);                              \
		fprintf(_f, "Function: %s\n", __func__);                              \
		fprintf(_f, "Message:  " fmt "\n", ##__VA_ARGS__);                    \
		fprintf(_f, "====================\n");                                \
                                                                                      \
		if (_f != stderr)                                                     \
			fclose(_f);                                                   \
                                                                                      \
		/* also echo to stderr so it's visible in terminal */                 \
		fprintf(stderr, "[FATAL] %s:%d (%s): " fmt "\n", __FILE__, __LINE__,  \
			__func__ __VA_OPT__(, ) __VA_ARGS__);                         \
                                                                                      \
		abort();                                                              \
	} while (0)

#ifdef __cplusplus
}
#endif
