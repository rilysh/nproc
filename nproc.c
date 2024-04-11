/* Published under GNU All-permissive license. */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <err.h>
#include <errno.h>
#include <getopt.h>

#if defined (__linux__)
# include <sched.h>
#elif defined (__FreeBSD__) || defined (__DragonFly__)
# include <sys/cpuset.h>
#elif defined (__OpenBSD__)
# include <sys/types.h>
# include <sys/sysctl.h>
#endif

/* Program name and version. */
#undef PROGNAME
#undef PROGVER
#define PROGNAME    "nproc"
#define PROGVER     "0.1"

/* Argument options structure. */
struct arg_opts {
	int aopt;
	int oopt;
	int uopt;
	int iopt;
	int hopt;
	int vopt;
};

/* Constants. */
enum {
	OPT_ALL      = 1,
	OPT_ONLINE   = 2,
	OPT_USABLE   = 3,
	OPT_IGNORE   = 4,
	OPT_HELP     = 5,
	OPT_VERSION  = 6,
	OPT_NONE     = 0
};

/* Count usable CPU, constrained by process CPU affinity or for the
   entire system. Note that, on OpenBSD there is currently no way to
   know or set CPU affinity, so it will only return number of CPUs
   that are online. */
static int count_usable_cpus(void)
{
	int ret, cpus;

/* For GNU/Linux. */
#if defined (__linux__)
        cpu_set_t cpu_set;

	CPU_ZERO(&cpu_set);
        ret = sched_getaffinity((pid_t)0, sizeof(cpu_set), &cpu_set);
	if (ret == -1)
		err(EXIT_FAILURE, "sched_getaffinity()");

        cpus = CPU_COUNT(&cpu_set);
        return (cpus);

/* For FreeBSD. */
#elif defined (__FreeBSD__) || defined (__DragonFly__)
	cpuset_t cpu_set;

	CPU_ZERO(&cpu_set);
	ret = cpuset_getaffinity(
		CPU_LEVEL_WHICH, CPU_WHICH_PID, (id_t)-1,
		sizeof(cpu_set), &cpu_set);
	if (ret == -1)
		err(EXIT_FAILURE, "cpuset_getaffinity()");

	cpus = CPU_COUNT(&cpu_set);
	return (cpus);

/* For OpenBSD (see the above note). */
#elif defined (__OpenBSD__)
	ret = sysconf(_SC_NPROCESSORS_ONLN);
	if (ret == -1)
		err(EXIT_FAILURE, "sysconf()");

	return (ret);

/* I don't know what operating system you're using. */	
#else
# error Unsupported operating system.
#endif
}

/* Count either the total number of CPUs or the number of
   CPUs that are online. Note that, on OpenBSD, _SC_NPROCESSORS_CONF
   and _SC_NPROCESSORS_ONLN doesn't show how many CPUs core
   are available, so we've do query with sysctl for that. */
static int get_count_type_cpus(int query)
{
	long cpus;

/* For GNU/Linux and FreeBSD. */
#if defined (__linux__) || defined (__FreeBSD__) || defined (__DragonFly__)
	cpus = sysconf(query);
	if (cpus == -1)
		err(EXIT_FAILURE, "sysconf()");

	return ((int)cpus);

/* For OpenBSD. */
#elif defined (__OpenBSD__)
	int mib[2], ret;
	size_t sz;

	mib[0] = CTL_HW;
	mib[1] = query;
	ret = sysctl(mib, 2, (long)&cpus, &sz, NULL, 0);
	if (ret == -1)
		err(EXIT_FAILURE, "sysctl()");
	
	return (cpus);

/* I don't know what operating system you're using. */	
#else
# error Unsupported operating system.
#endif
}

/* "Safe" atoi wrapper around strtol(). */ 
static int safe_atoi(const char *s)
{
	long ret;
	char *eptr;

	errno = 0;
	ret = strtol(s, &eptr, 10);

	if (errno != 0 || eptr == s || ret < 0)
		errx(EXIT_FAILURE, "invalid number: '%s'", s);

	return ((int)ret);
}

/* The help menu. */
static void show_usage(int out_okay)
{
	fputs("usage: "PROGNAME
	      " [--all] [--online] [--usable] [--ignore=COUNT]\n"
	      "       [--help] [--version]\n",
	      out_okay ? stdout : stderr);
}

int main(int argc, char **argv)
{
	struct option lopts[] = {
		{ "all",      no_argument,        NULL, OPT_ALL },
		{ "online",   no_argument,        NULL, OPT_ONLINE },
		{ "usable",   no_argument,        NULL, OPT_USABLE },
		{ "ignore",   required_argument,  NULL, OPT_IGNORE },
		{ "help",     no_argument,        NULL, OPT_HELP },
		{ "version",  no_argument,        NULL, OPT_VERSION },
		{ NULL,       0,                  NULL, OPT_NONE },
	};
	struct arg_opts opts;
	int c, cpus, sc, inum;

	/* If no argument is provided, get all usable CPUs (affinity). */
	if (argc < 2) {
		cpus = count_usable_cpus();
		/* We'll have an argument if we pass "--ignore", so
		 this section will be skipped to the last branch. */
		fprintf(stdout, "%d\n", cpus);
		exit(EXIT_SUCCESS);
	}

	/* Zero fill every members to that structure. */
	memset(&opts, '\0', sizeof(struct arg_opts));

	/* To turn off Clang's warning of uninitialized variable. */
	inum = 0;
	for (;;) {
	        c = getopt_long(argc, argv, "", lopts, NULL);
		if (c == -1)
		        break;

		switch (c) {
		case OPT_ALL:
			/* Option: --all */
			opts.aopt = 1;
			break;

		case OPT_ONLINE:
			/* Option: --online */
			opts.oopt = 1;
			break;

		case OPT_USABLE:
			/* Option: --usable */
			opts.uopt = 1;
			break;

		case OPT_IGNORE:
			/* Option: --ignore */
		        inum = safe_atoi(optarg);
			opts.iopt = 1;
			break;

		case OPT_HELP:
			/* Option: --help */
		        opts.hopt = 1;
			break;

		case OPT_VERSION:
			/* Option: --version */
			opts.vopt = 1;
			break;

		default:
			/* Wrong argument. */
		        exit(EXIT_FAILURE);
		}
	}

	/* Option: --all (without --ignore) */
	if (opts.iopt == 0 && opts.aopt) {
#if defined (__OpenBSD__)
		cpus = get_count_type_cpus(HW_NCPUFOUND);
#else
		cpus = get_count_type_cpus(_SC_NPROCESSORS_CONF);
#endif
		fprintf(stdout, "%d\n", cpus);
	}

	/* Option: --online (without --ignore) */
	else if (opts.iopt == 0 && opts.oopt) {
		cpus = get_count_type_cpus(_SC_NPROCESSORS_ONLN);
		fprintf(stdout, "%d\n", cpus);
	}

	/* Option: --usable (without --ignore) */
	else if (opts.iopt == 0 && opts.uopt) {
		cpus = count_usable_cpus();
		fprintf(stdout, "%d\n", cpus);
	}

	/* Option: --ignore */
	else if (opts.iopt) {
		/* If no explicit option is provided use, --usable option
		   as the default one. */
		if (opts.aopt == 0 && opts.oopt == 0 && opts.uopt == 0)
		        opts.uopt = 1;

		/* Option: --usable */
	        if (opts.uopt) {
			cpus = count_usable_cpus();
			if (inum > cpus)
				cpus = 1;
			else
				cpus -= inum;
		} else {
			/* If "--all" is provided use _SC_NPROCESSORS_CONF
			   otherwise expect "--online" is provided so use
			   _SC_NPROCESSORS_ONLN. */
		        sc = opts.aopt ?
				_SC_NPROCESSORS_CONF :
				_SC_NPROCESSORS_ONLN;

			cpus = get_count_type_cpus(sc);
			if (inum > cpus)
				cpus = 1;
			else
				cpus -= inum;
		}

		fprintf(stdout, "%d\n", cpus);
        }

	/* Option: --help */
	else if (opts.hopt) {
		show_usage(1);
	}

	/* Option: --version */
	else if (opts.vopt) {
		fputs("nproc "PROGVER"\n", stdout);
	}

	/* Default (no option is provided). */
	else {
		cpus = count_usable_cpus();
		fprintf(stdout, "%d\n", cpus);
	}

	exit(EXIT_SUCCESS);
}
