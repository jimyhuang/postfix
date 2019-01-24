/*++
/* NAME
/*	postlogd 8
/* SUMMARY
/*	Postfix internal log server
/* SYNOPSIS
/*	\fBpostlogd\fR [generic Postfix daemon options]
/* DESCRIPTION
/*	This program logs events on behalf of Postfix programs
/*	when the maillog configuration parameter specifies a non-empty
/*	value.
/* BUGS
/*	Non-daemon Postfix programs don't know that they should log
/*	to the internal logging service until after they have
/*	processed command-line options and main.cf parameters. These
/*	programs still log earlier events to the syslog service.
/*
/*	If Postfix is down, then logging from non-daemon programs
/*	will be lost, except for logging from the \fBpostfix\fR(1),
/*	\fBpostlog\fR(1), and \fBpostsuper\fR(1) commands. These
/*	commands can log directly to file when running as root, for
/*	example during Postfix start-up.
/*
/*	Non-daemon Postfix programs can talk to \fBpostlogd\fR(8)
/*	only if they are run by the super-user, or if their executable
/*	files have set-gid permission.
/* CONFIGURATION PARAMETERS
/* .ad
/* .fi
/*	Changes to \fBmain.cf\fR are picked up automatically, as
/*	\fBpostlogd\fR(8) processes run for only a limited amount
/*	of time. Use the command "\fBpostfix reload\fR" to speed
/*	up a change.
/*
/*	The text below provides only a parameter summary. See
/*	\fBpostconf\fR(5) for more details including examples.
/* .IP "\fBconfig_directory (see 'postconf -d' output)\fR"
/*	The default location of the Postfix main.cf and master.cf
/*	configuration files.
/* .IP "\fBmaillog_file (empty)\fR"
/*	The name of an optional logfile that is written by the \fBpostlogd\fR(8)
/*	internal logging service.
/* .IP "\fBprocess_id (read-only)\fR"
/*	The process ID of a Postfix command or daemon process.
/* .IP "\fBprocess_name (read-only)\fR"
/*	The process name of a Postfix command or daemon process.
/* .IP "\fBsyslog_name (see 'postconf -d' output)\fR"
/*	A prefix that is prepended to the process name in syslog
/*	records, so that, for example, "smtpd" becomes "prefix/smtpd".
/* .IP "\fBservice_name (read-only)\fR"
/*	The master.cf service name of a Postfix daemon process.
/* .IP "\fBpostlogd_watchdog_timeout (10s)\fR"
/*	How much time a \fBpostlogd\fR(8) process may take to process a request
/*	before it is terminated by a built-in watchdog timer.
/* SEE ALSO
/*	postconf(5), configuration parameters
/*	syslogd(5), system logging
/* LICENSE
/* .ad
/* .fi
/*	The Secure Mailer license must be distributed with this software.
/* HISTORY
/* .ad
/* .fi
/*	This service was introduced with Postfix version 3.4.
/* AUTHOR(S)
/*	Wietse Venema
/*	Google, Inc.
/*	111 8th Avenue
/*	New York, NY 10011, USA
/*--*/

 /*
  * System library.
  */
#include <sys_defs.h>
#include <syslog.h>	/* TEMPORARY */

 /*
  * Utility library.
  */
#include <logwriter.h>
#include <msg.h>
#include <msg_logger.h>
#include <vstream.h>

 /*
  * Global library.
  */
#include <mail_params.h>
#include <mail_version.h>

 /*
  * Server skeleton.
  */
#include <mail_server.h>

 /*
  * Tunable parameters.
  */
int     var_postlogd_watchdog;

 /*
  * Silly little macros.
  */
#define STR(x)			vstring_str(x)
#define LEN(x)			VSTRING_LEN(x)

 /*
  * Logfile stream.
  */
static VSTREAM *postlogd_stream = 0;

/* postlogd_fallback - log messages from postlogd(8) itself */

static void postlogd_fallback(const char *buf)
{
    (void) logwriter_write(postlogd_stream, buf, strlen(buf));
}

/* postlogd_service - perform service for client */

static void postlogd_service(char *buf, ssize_t len, char *unused_service,
			             char **unused_argv)
{

    /*
     * This service may still receive messages after "postfix reload" with a
     * configuration that removes the maillog_file setting. Those messages
     * will have to be syslogged instead.
     * 
     * XXX When forwarding to syslogd(8), don't bother stripping the time stamp
     * from the preformatted record: we'd have to deal with short records. If
     * we must make our presence invisible, msg_logger(3) should send time in
     * seconds, and leave the formatting to postlogd(8).
     */
    if (postlogd_stream) {
	(void) logwriter_write(postlogd_stream, buf, len);
    } else {
	/* Until msg_logger has a 'shut up' feature. */
	syslog(LOG_MAIL | LOG_INFO, "%.*s", (int) len, buf);	/* TEMPORARY */
    }
}

/* pre_jail_init - pre-jail handling */

static void pre_jail_init(char *unused_service_name, char **argv)
{

    /*
     * During process initialization, the postlogd daemon will log events to
     * the postlog socket, so that they can be logged to file later. Once the
     * postlogd daemon is handling requests, it will stop logging to the
     * postlog socket and will instead write to the logfile, to avoid
     * infinite recursion.
     */

    /*
     * Sanity check. This service takes no command-line arguments.
     */
    if (argv[0])
	msg_fatal("unexpected command-line argument: %s", argv[0]);

    /*
     * This service may still receive messages after "postfix reload" into a
     * configuration that no longer specifies a maillog file. Those messages
     * will have to be syslogged instead.
     */
    if (*var_maillog_file != 0) {

	/*
	 * Instantiate the logwriter or bust.
	 */
	postlogd_stream = logwriter_open(var_maillog_file);

	/*
	 * Inform the msg_logger client to stop using the postlog socket, and
	 * to call our logwriter.
	 */
	msg_logger_control(CA_MSG_LOGGER_CTL_FALLBACK_ONLY,
			   CA_MSG_LOGGER_CTL_FALLBACK_FN(postlogd_fallback),
			   CA_MSG_LOGGER_CTL_END);
    }
}

/* post_jail_init - post-jail initialization */

static void post_jail_init(char *unused_name, char **unused_argv)
{

    /*
     * Prevent automatic process suicide after a limited number of client
     * requests. It is OK to terminate after a limited amount of idle time.
     */
    var_use_limit = 0;
}

MAIL_VERSION_STAMP_DECLARE;

/* main - pass control to the multi-threaded skeleton */

int     main(int argc, char **argv)
{
    static const CONFIG_TIME_TABLE time_table[] = {
	VAR_POSTLOGD_WATCHDOG, DEF_POSTLOGD_WATCHDOG, &var_postlogd_watchdog, 10, 0,
	0,
    };

    /*
     * Fingerprint executables and core dumps.
     */
    MAIL_VERSION_STAMP_ALLOCATE;

    dgram_server_main(argc, argv, postlogd_service,
		      CA_MAIL_SERVER_TIME_TABLE(time_table),
		      CA_MAIL_SERVER_PRE_INIT(pre_jail_init),
		      CA_MAIL_SERVER_POST_INIT(post_jail_init),
		      CA_MAIL_SERVER_SOLITARY,
		      CA_MAIL_SERVER_WATCHDOG(&var_postlogd_watchdog),
		      0);
}
