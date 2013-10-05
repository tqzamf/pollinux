#ifndef USER_BACKTRACE_H
	#define USER_BACKTRACE_H

	#ifdef CONFIG_DEBUG_USER
		void user_backtrace_impl(struct pt_regs *regs, const char *file, int line);
		#define user_backtrace(X) user_backtrace_impl((X), __FILE__, __LINE__)
	#else
		#define user_backtrace(X) do {} while(0)
	#endif

#endif // USER_BACKTRACE_H

