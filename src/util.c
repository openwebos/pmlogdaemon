/* @@@LICENSE
*
*      Copyright (c) 2007-2013 LG Electronics, Inc.
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*
* LICENSE@@@ */

/**
 ***********************************************************************
 * @file util.c
 *
 * @brief This file contains generic utility functions.
 *
 ***********************************************************************
 */

#include "main.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>


/**
 * @brief mystrcpy
 *
 * Easy to use wrapper for strcpy to make it safe against buffer
 * overflows and to report any truncations.
 *
 * @param dst
 * @param dstSize
 * @param src
 */
void mystrcpy(char* dst, size_t dstSize, const char* src)
{
	size_t	srcLen;

	if (dst == NULL)
	{
		ErrPrint("mystrcpy null dst\n");
		return;
	}

	if (dstSize < 1)
	{
		ErrPrint("mystrcpy invalid dst size\n");
		return;
	}

	dst[ 0 ] = 0;

	if (src == NULL)
	{
		ErrPrint("mystrcpy null src\n");
		return;
	}

	srcLen = strlen(src);
	if (srcLen >= dstSize)
	{
		ErrPrint("mystrcpy buffer overflow on '%s'\n", src);
		srcLen = dstSize - 1;
	}

	memcpy(dst, src, srcLen);
	dst[ srcLen ] = 0;
}


/**
 * @brief mystrcat
 *
 * Easy to use wrapper for strcat to make it safe against buffer
 * overflows and to report any truncations.
 *
 * @param dst
 * @param dstSize
 * @param src
 */
void mystrcat(char* dst, size_t dstSize, const char* src)
{
	size_t	dstLen;
	size_t	srcLen;
	size_t	maxLen;

	if (dst == NULL)
	{
		ErrPrint("mystrcat null dst\n");
		return;
	}

	if (dstSize < 1)
	{
		ErrPrint("mystrcat invalid dst size\n");
		return;
	}

	dstLen = strlen(dst);
	if (dstLen >= dstSize)
	{
		ErrPrint("mystrcat invalid dst len\n");
		return;
	}

	if (src == NULL)
	{
		ErrPrint("mystrcat null src\n");
		return;
	}

	srcLen = strlen(src);
	if (srcLen < 1)
	{
		/* empty string, do nothing */
		return;
	}

	maxLen = (dstSize - 1) - dstLen;

	if (srcLen > maxLen)
	{
		ErrPrint("mystrcat buffer overflow\n");
		srcLen = maxLen;
	}

	if (srcLen > 0)
	{
		memcpy(dst + dstLen, src, srcLen);
		dst[ dstLen + srcLen ] = 0;
	}
}


/**
 * @brief mysprintf
 *
 * Easy to use wrapper for sprintf to make it safe against buffer
 * overflows and to report any truncations.
 *
 * @param dst
 * @param dstSize
 * @param fmt
 * @param ...
 */
void mysprintf(char* dst, size_t dstSize, const char* fmt, ...)
{
	va_list 		args;
	int				n;

	if (dst == NULL)
	{
		ErrPrint("mysprintf null dst\n");
		return;
	}

	if (dstSize < 1)
	{
		ErrPrint("mysprintf invalid dst size\n");
		return;
	}

	dst[ 0 ] = 0;

	if (fmt == NULL)
	{
		ErrPrint("mysprintf null fmt\n");
		return;
	}

	va_start(args, fmt);

	n = vsnprintf(dst, dstSize, fmt, args);
	if (n < 0)
	{
		ErrPrint("mysprintf error\n");
		dst[ 0 ] = 0;
	}
	else if (((size_t) n) >= dstSize)
	{
		ErrPrint("mysprintf buffer overflow\n");
		dst[ dstSize - 1 ] = 0;
	}

	va_end(args);
}

typedef struct
{
	char	path[ PATH_MAX ];
	int		fd;
}
LockFile;

static LockFile	g_processLock;


/**
 * @brief LockProcess
 *
 * Acquire the process lock (by getting an file lock on our pid file).
 *
 * @param component
 *
 * @return true on success, false if failed.
 */
bool LockProcess(const char* component)
{
	const char* locksDirPath = "/tmp/run";

	LockFile*	lock;
	pid_t		pid;
	int			fd;
	int			result;
	char		pidStr[ 16 ];
	int			pidStrLen;
	int			err;

	lock = &g_processLock;
	pid = getpid();

	/* create the locks directory if necessary */
	(void) mkdir(locksDirPath, 0777);

	mysprintf(lock->path, sizeof(lock->path), "%s/%s.pid", locksDirPath,
		component);

	/* open or create the lock file */
	fd = open(lock->path, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
	if (fd < 0)
	{
		err = errno;
		ErrPrint("Failed to open lock file (err %d, %s), exiting.\n",
			err, strerror(err));
		return false;
	}

	/* use a POSIX advisory file lock as a mutex */
	result = lockf(fd, F_TLOCK, 0);
	if (result < 0)
	{
		err = errno;
		if ((err == EDEADLK) || (err == EAGAIN))
		{
			ErrPrint("Failed to acquire lock, exiting.\n");
		}
		else
		{
			ErrPrint("Failed to acquire lock (err %d, %s), exiting.\n",
				err, strerror(err));
		}
		return false;
	}

	/* remove the old pid number data */
	result = ftruncate(fd, 0);
	if (result < 0)
	{
		err = errno;
		DbgPrint("Failed truncating lock file (err %d, %s).\n",
			err, strerror(err));
	}

	/* write the pid to the file to aid debugging */
	mysprintf(pidStr, sizeof(pidStr), "%d\n", pid);
	pidStrLen = (int) strlen(pidStr);
	result = write(fd, pidStr, pidStrLen);
	if (result < pidStrLen)
	{
		err = errno;
		DbgPrint("Failed writing lock file (err %d, %s).\n",
			err, strerror(err));
	}

	lock->fd = fd;
	return true;
}


/**
 * @brief UnlockProcess
 *
 * Release the lock on the pid file as previously acquired by
 * LockProcess.
 */
void UnlockProcess(void)
{
	LockFile*	lock;

	lock = &g_processLock;
	close(lock->fd);
	(void) unlink(lock->path);
}


/**
 * @brief TrimSuffixCaseInsensitive
 *
 * @param s
 * @param suffix
 *
 * @return
 */
bool TrimSuffixCaseInsensitive(char* s, const char* suffix)
{
	size_t	sLen;
	size_t	suffixLen;
	char*	sSuffix;

	sLen = strlen(s);
	suffixLen = strlen(suffix);
	if (sLen < suffixLen)
	{
		return false;
	}

	sSuffix = s + sLen - suffixLen;
	if (strcasecmp(sSuffix, suffix) != 0)
	{
		return false;
	}

	*sSuffix = 0;
	return true;
}


/**
 * @brief ParseInt
 *
 * @param valStr
 * @param nP
 *
 * @return
 */
bool ParseInt(const char* valStr, int* nP)
{
	long int	n;
	char*		endptr;

	endptr = NULL;
	errno = 0;
	n = strtol(valStr, &endptr, 0);
	if ((endptr == valStr) || (*endptr != 0) || (errno != 0))
	{
		return false;
	}

	*nP = (int) n;
	return true;
}

/**
 * @brief ParseLevel
 *
 * "none" => -1 (kPmLogLevel_None)
 * "err"  => LOG_ERR (kPmLogLevel_Error),
 * etc.
 *
 * @param s
 * @param levelP
 *
 * @return true if parsed OK, else false.
 */
bool ParseLevel(const char* s, int* levelP)
{
	const int* nP;

	nP = PmLogStringToLevel(s);
	if (nP != NULL)
	{
		*levelP = *nP;
		return true;
	}

	*levelP = -1;
	return false;
}

/**
 * @brief ParseSize
 *
 * Wrapper for ParseInt, but allows specifying 'K'/'KB' at the end as a
 * multiplier.
 *
 * @param valStr
 * @param nP
 *
 * @return
 */
bool ParseSize(const char* valStr, int* nP)
{
	char	s[ 32 ];
	int		multiplier;

	mystrcpy(s, sizeof(s), valStr);

	multiplier = 1;

	if (TrimSuffixCaseInsensitive(s, "K") ||
		TrimSuffixCaseInsensitive(s, "KB"))
	{
		multiplier = 1024;
	}

	if (TrimSuffixCaseInsensitive(s, "M") ||
		TrimSuffixCaseInsensitive(s, "MB"))
	{
		multiplier = 1024 * 1024;
	}

	if (!ParseInt(s, nP))
	{
		return false;
	}

	*nP = *nP * multiplier;
	return true;
}


/**
 * @brief ParseKeyValue
 *
 * If the given string is of the form "KEY=VALUE" copy the given
 * key and value strings into the specified buffers and return true,
 * otherwise return false.
 * Key may not be empty string, but value may be.
 *
 * @param arg
 * @param keyBuff
 * @param keyBuffSize
 * @param valBuff
 * @param valBuffSize
 *
 * @return
 */
bool ParseKeyValue(const char* arg, char* keyBuff, size_t keyBuffSize,
	char* valBuff, size_t valBuffSize)
{
	const char* sepStr;
	size_t		keyLen;
	const char* valStr;
	size_t		valLen;

	sepStr = strchr(arg, '=');
	if ((sepStr == NULL) || (sepStr <= arg))
		return false;

	keyLen = sepStr - arg;
	if (keyLen >= keyBuffSize)
		return false;

	memcpy(keyBuff, arg, keyLen);
	keyBuff[keyLen] = 0;

	valStr = sepStr + 1;
	valLen = strlen(valStr);
	if (valLen >= valBuffSize)
		return false;

	memcpy(valBuff, valStr, valLen);
	valBuff[valLen] = 0;

	return true;
}
