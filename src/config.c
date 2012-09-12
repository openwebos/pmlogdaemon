/* @@@LICENSE
*
*      Copyright (c) 2007-2012 Hewlett-Packard Development Company, L.P.
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
 *********************************************************************
 * @file config.c
 *
 * @brief This file contains the functions to read the PmLog.conf
 * configuration file.
 *
 ***********************************************************************
 */


#include "main.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syslog.h>
#include <glib.h>


/***********************************************************************
 * global configuration settings
 ***********************************************************************/
int				g_numOutputs;
PmLogFile_t		g_outputConfs[ PMLOG_MAX_NUM_OUTPUTS ];

int				g_numContexts;
GTree 			*g_contextConfs = NULL;

/***********************************************************************
 * OUTPUT section parsing

	Example configuration section:

		[OUTPUT=kernlog]
		File=/var/log/kern.log
		MaxSize=100K
		Rotations=1

	Required:
	 	File=/var/log/kern.log

	Optional:
		MaxSize=100K
	 	Rotations=1
 ***********************************************************************/


typedef struct
{
	char	name[ PMLOG_OUTPUT_MAX_NAME_LENGTH + 1 ];
	char	File[ PATH_MAX ];
	int	MaxSize;
	int	Rotations;
}
PmLogParseOutput_t;


/**
 * @brief FindOutputByName
 * Look up the named output in the g_outputConfs array.
 *
 * @param outputName  name of output
 * @param indexP index to set to correct value
 *
 * @return the index or -1 if not found.
 */
static const PmLogFile_t* FindOutputByName(const char* outputName,
	int* indexP)
{
	DbgPrint("%s called with outputName=%s\n",
			__FUNCTION__, outputName);
	int							i;
	const PmLogFile_t*	outputConfP;

	for (i = 0; i < g_numOutputs; i++)
	{
		outputConfP = &g_outputConfs[ i ];
		if (outputConfP && outputConfP->outputName &&
				(strcmp(outputConfP->outputName,
					outputName) == 0))
		{
			if (indexP != NULL)
			{
				*indexP = i;
			}

			return outputConfP;
		}
	}

	if (indexP != NULL)
	{
		*indexP = -1;
	}

	return NULL;
}

/**
 * @brief ConfIntValueOrDefault
 *
 * @param n
 * @param defaultVal
 *
 * @return
 */
inline int ConfIntValueOrDefault(int n, int defaultVal)
{
	return (n == CONF_INT_UNINIT_VALUE) ? defaultVal : n;
}


/**
 * @brief GetTokenToSep
 *
 * @param sP
 * @param token
 * @param tokenBuffSize
 * @param terminators
 * @param terminatorP
 */
static void GetTokenToSep(const char** sP, char* token, size_t tokenBuffSize,
	const char* terminators, char* terminatorP)
{
	const char*		s;
	size_t			tokenLen;

	s = *sP;
	tokenLen = 0;

	while ((*s != 0) && (strchr(terminators, *s) == NULL))
	{
		if (tokenLen + 1 >= tokenBuffSize)
		{
			/* token truncated */
		}
		else
		{
			token[ tokenLen ] = *s;
			tokenLen++;
		}
		s++;
	}

	token[ tokenLen ] = 0;
	*terminatorP = *s;
	if (*s != 0)
	{
		s++;
	}
	*sP = s;
}


/**
 * @brief ParseOutputInit
 *
 * @param name
 * @param parseOutputP
 *
 * @return
 */
static bool ParseOutputInit(const char* name, PmLogParseOutput_t* parseOutputP)
{
	memset(parseOutputP, 0, sizeof(PmLogParseOutput_t));
	DbgPrint("%s called with name=%s\n",
			__FUNCTION__, name);

	if (g_numOutputs == 0)
	{
		/* we require that first output be stdlog */
		if (strcmp(PMLOG_OUTPUT_STDLOG, name) != 0)
		{
			ErrPrint("Expected stdlog definition\n");
			return false;
		}
	}
	/* need to check that name is valid length and char set */
	mystrcpy(parseOutputP->name, sizeof(parseOutputP->name), name);
	parseOutputP->File[ 0 ]		= 0;
	parseOutputP->MaxSize		= CONF_INT_UNINIT_VALUE;
	parseOutputP->Rotations		= CONF_INT_UNINIT_VALUE;

	return true;
}


/**
 * @brief MakeOutputConf
 *
 * This is the constructor for the OutputConf object.  It will
 * convert the values in the PmLogParseOutput_t into a PmLogFile_t
 * object.
 *
 * This method will enforce value formatting and limits restrictions
 *
 * @param parseOutputP the ParseOutput object containing the init values.
 *
 * @return true iff the OutputConf was added to the g_outputConfs array.
 */
static bool MakeOutputConf(PmLogParseOutput_t* parseOutputP)
{
	DbgPrint("%s called with po.name=%s\n",
			__FUNCTION__, parseOutputP->name);
	PmLogFile_t* outputConfP;
	int i;

	switch (parseOutputP->File[0]) {
		case 0:
			ErrPrint("%s: File not specified\n", parseOutputP->name);
			return false;
		case '/':
			break;
		default:
			ErrPrint("%s: Expected File full path value\n", parseOutputP->name);
			return false;
	}

	/* Finding output */
    outputConfP = (PmLogFile_t *) FindOutputByName(parseOutputP->name, &i);

	/* Make new one */
	if (NULL == outputConfP) {
		DbgPrint("%s creating %d)st output\n",__FUNCTION__, g_numOutputs+1);
		if (g_numOutputs >= PMLOG_MAX_NUM_OUTPUTS) {
			ErrPrint("%s: Too many output definitions\n", parseOutputP->name);
			return false;
		}
		outputConfP = &g_outputConfs[g_numOutputs];
		memset(outputConfP, 0, sizeof(PmLogFile_t));
		outputConfP->outputName = g_strdup(parseOutputP->name);
		outputConfP->path = g_strdup(parseOutputP->File);
		g_numOutputs++;
	}

	/*
	 * Note: we are not changing the outputName or
	 * path if this context already existed
	 */

	if (parseOutputP->MaxSize == CONF_INT_UNINIT_VALUE) {
		/* not set by conf file - set to default */
		parseOutputP->MaxSize = PMLOG_DEFAULT_LOG_SIZE;
	} else {
		if (parseOutputP->MaxSize < PMLOG_MIN_LOG_SIZE) {
			ErrPrint("%s: Log size must be > 4KB: setting to that minimum\n", parseOutputP->name);
			parseOutputP->MaxSize = PMLOG_MIN_LOG_SIZE;
		} else if (parseOutputP->MaxSize > PMLOG_MAX_LOG_SIZE) {
			ErrPrint("%s: Log size must be < 64MB: setting to that maximum\n", parseOutputP->name);
			parseOutputP->MaxSize = PMLOG_MAX_LOG_SIZE;
		}
	}

	if (parseOutputP->Rotations == CONF_INT_UNINIT_VALUE) {
		/* not set - make default */
		outputConfP->rotations = PMLOG_DEFAULT_LOG_ROTATIONS;
	} else {
		if (parseOutputP->Rotations < PMLOG_MIN_NUM_ROTATIONS) {
			ErrPrint("%s: Rotations must be >= %d: setting to that minimum\n", parseOutputP->name, PMLOG_MIN_NUM_ROTATIONS);
			parseOutputP->Rotations = PMLOG_MIN_NUM_ROTATIONS;
		} else if (parseOutputP->Rotations > PMLOG_MAX_NUM_ROTATIONS) {
			ErrPrint("%s: Rotations must be between <= %d: setting to that maximum\n", parseOutputP->name, PMLOG_MAX_NUM_ROTATIONS);
			parseOutputP->Rotations = PMLOG_MAX_NUM_ROTATIONS;
		}
	}
	return true;
}

/***********************************************************************
 * OUTPUT section parsing

	Example configuration section:

		[CONTEXT=<global>]
		Rule1=*.*,stdlog
		Rule2=kern.*,kernlog
		Rule3=*.err,errlog
 ***********************************************************************/


typedef struct
{
	/* -1 = all or specific value e.g. LOG_KERN */
	int			facility;

	/* -1 = all or specific value e.g. LOG_ERR */
	int			level;
	bool		levelInvert;

	/* empty = all or specific value */
	char		program[ PMLOG_PROGRAM_MAX_NAME_LENGTH + 1 ];

	/* index of output target */
	int			outputIndex;

	/* false to include, true to omit */
	bool		omitOutput;
}
PmLogParseRule_t;


typedef struct
{
	char				name[ PMLOG_MAX_CONTEXT_NAME_LEN + 1 ];
	int					numRules;
	int 			bufferSize;
	int			flushLevel;
	PmLogParseRule_t	rules[ PMLOG_CONTEXT_MAX_NUM_RULES ];
}
PmLogParseContext_t;


/**
 * @brief ParseContextInit
 *
 * Initializer for the Parser's Context object
 *
 * @param name name of the object
 * @param parseContextP the pointer to the object to initialize
 *
 * @return true iff we were able to initialize the object
 */
static bool ParseContextInit
(const char* name, PmLogParseContext_t* parseContextP)
{
	memset(parseContextP, 0, sizeof(PmLogParseContext_t));
	DbgPrint("%s called with name=%s\n",
			__FUNCTION__, name);

	if (g_numContexts == 0)
	{
		/* we require that first context be the global context */
		if (strcmp(PMLOG_CONTEXT_GLOBAL, name) != 0)
		{
			ErrPrint("Expected global context definition\n");
			return false;
		}
	}

	mystrcpy(parseContextP->name, sizeof(parseContextP->name), name);
	parseContextP->numRules = 0;

	return true;
}


/**
 * @brief ParseContextData
 *
 * @param parseContextP
 * @param key
 * @param val
 *
 * @return true if parsed OK, else set error message.
 */
static bool ParseContextData(
		PmLogParseContext_t* parseContextP, const char* key,
		const char* val)
{
	PmLogParseRule_t*		parseRuleP;
	const char*				s;
	char					token[ 32 ];
	char					sep;

	DbgPrint("%s called with key=%s\n",__FUNCTION__, key);

	parseRuleP = &parseContextP->rules[ parseContextP->numRules ];

	/*
	 * value string should be of the form <filter>,<output>
	 * where filter ::= <facility>[.[!]<level>[.<program>]]
	 */
	s = val;

	/* get facility */
	GetTokenToSep(&s, token, sizeof(token), ".,", &sep);
	if (!ParseRuleFacility(token, &parseRuleP->facility))
	{
		ErrPrint("Facility not parsed: '%s'\n", token);
		return false;
	}

	/* get level (optional) */
	if (sep == '.')
	{
		parseRuleP->levelInvert = false;
		if (*s == '!')
		{
			parseRuleP->levelInvert = true;
			s++;
		}

		GetTokenToSep(&s, token, sizeof(token), ".,", &sep);
		if (!ParseRuleLevel(token, &parseRuleP->level))
		{
			ErrPrint("Level not parsed: '%s'\n", token);
			return false;
		}
	}
	else
	{
		parseRuleP->levelInvert = false;
		parseRuleP->level = -1;
	}

	/* get program (optional) */
	if (sep == '.')
	{
		GetTokenToSep(&s, token, sizeof(token), ".,", &sep);
		mystrcpy(parseRuleP->program, sizeof(parseRuleP->program), token);
	}
	else
	{
		parseRuleP->program[0] = 0;
	}

	/* we should be at the ',' separator between <filter> and <output> */
	if (sep != ',')
	{
		ErrPrint("Expected ',' after filter\n");
		return false;
	}

	parseRuleP->omitOutput = false;
	if (*s == '-')
	{
		parseRuleP->omitOutput = true;
		s++;
	}

	GetTokenToSep(&s, token, sizeof(token), ".,", &sep);
	if (FindOutputByName(token, &parseRuleP->outputIndex) == NULL)
	{
		ErrPrint("Output not recognized: '%s'\n", token);
		return false;
	}

	if (sep != 0)
	{
		ErrPrint("Unexpected data after output\n");
		return false;
	}

	parseContextP->numRules++;

	return true;
}


static PmLogContextConf_t * CreateContext(const char* name) {
	PmLogContextConf_t*		contextConfP;
	gchar * gName = g_strdup(name);
    contextConfP = g_new0(PmLogContextConf_t, 1);
	if (contextConfP == NULL) {
		ErrPrint("%s: Failed to malloc\n", __FUNCTION__);
		abort();
	}
	contextConfP->contextName = gName;
	g_tree_insert(g_contextConfs, gName, contextConfP);
	g_numContexts = g_tree_nnodes(g_contextConfs);
	return contextConfP;
}


/**
 * @brief MakeContextConf
 *
 * This is the constructor for the ContextConf object.  It will
 * convert the values in the PmLogParseContext_t into a PmLogContextConf_t
 * object.
 *
 * @param parseContextP the ParseContext object containing the init values.
 *
 * @return true iff the ContextConf was added to the g_contextConfs array.
 */
static bool MakeContextConf(PmLogParseContext_t* parseContextP)
{
	PmLogContextConf_t*		contextConfP;
	int						i;
	PmLogRule_t*			contextRuleP;
	PmLogParseRule_t*		parseRuleP;

	contextConfP = g_tree_lookup(g_contextConfs, parseContextP->name);

	if (NULL == contextConfP) {
		contextConfP = CreateContext(parseContextP->name);
	}

	/* copy over the rules */
	contextConfP->numRules = parseContextP->numRules;
	for (i = 0; i < parseContextP->numRules; i++)
	{
		contextRuleP = &contextConfP->rules[ i ];
		parseRuleP = &parseContextP->rules[ i ];

		contextRuleP->facility		= parseRuleP->facility;
		contextRuleP->level		= parseRuleP->level;
		contextRuleP->levelInvert	= parseRuleP->levelInvert;

		if ('\0' == parseRuleP->program[0]) {
			g_free(contextRuleP->program);
            contextRuleP->program = NULL;
        } else {
			contextRuleP->program = g_strdup(parseRuleP->program);
        }

		contextRuleP->outputIndex	= parseRuleP->outputIndex;
		contextRuleP->omitOutput	= parseRuleP->omitOutput;
	}

	/* copy buffer info */
	contextConfP->rb = RBNew(parseContextP->bufferSize,parseContextP->flushLevel);

	return true;
}


/**
 * @brief ClearConf
 * Erases all data in the configuration objects
 * (g_outputConfs and g_contextConfs)
 */
static void ClearConf(void)
{
	int i;
	for (i=0; i<g_numOutputs; i++) {
		g_free(g_outputConfs[i].outputName);
        g_outputConfs[i].outputName = NULL;
		g_free(g_outputConfs[i].path);
		g_outputConfs[i].path = NULL;
	}

	if (g_contextConfs != NULL)
		g_tree_destroy(g_contextConfs);

	g_numOutputs = 0;
	g_numContexts = 0;

	memset(&g_outputConfs, 0, sizeof(g_outputConfs));
	g_contextConfs = NULL;
}


/**
 * @brief ReadConfFile
 *
 * Read the value of the configuration file into the
 * PmLogFile_t and PmLogContextConf_t data structures.
 *
 * @param config_path the path to the config file
 *
 * @return true iff we were able to read the file
 */
bool
ReadConfFile(const char* config_path)
{
    GKeyFile *config_file = NULL;
    bool retVal = true;
    GError *gerror = NULL;
    gchar** groups = NULL;
    gsize numGroups;
    DbgPrint("%s called with config path = %s\n",
		    __FUNCTION__, config_path);

    config_file = g_key_file_new();
    if (!config_file)
    {
        ErrPrint("%s cannot create key file\n",
                __FUNCTION__);
	return false;
    }

    retVal = g_key_file_load_from_file(config_file, config_path,
        G_KEY_FILE_NONE, &gerror);
    if ( gerror ) {
	    ErrPrint("%s: error reading config file: %s\n", __FUNCTION__, gerror->message);
	    goto end;
    }
    if (!retVal)
    {
        ErrPrint("%s cannot load config file from %s\n",
                __FUNCTION__, config_path);
	goto end;
    }

    groups = g_key_file_get_groups (config_file, &numGroups);

    const char* outputPrefix = "OUTPUT=";
    const char* contextPrefix = "CONTEXT=";
    char * name;
    char ruleName[10];
    int i;
    int j;
    PmLogParseOutput_t parseOutput;
    PmLogParseContext_t parseContext;
    gchar* str;
    int		rots;
    for (i=0; i<numGroups; i++) {
	    if (g_str_has_prefix(groups[i], outputPrefix)) {

		    /* OUTPUT parsing */
		    name = groups[i]+strlen(outputPrefix);
		    ParseOutputInit(name, &parseOutput);

			gerror = NULL;
			str = g_key_file_get_string(config_file,groups[i], "File", &gerror);
			if (!gerror && (str != NULL)) {
				strncpy(parseOutput.File, str, sizeof(parseOutput.File) - 1);
				DbgPrint("read prop File = %s\n", str);
			} else {
				ErrPrint("error: %s\n", gerror->message);
				g_error_free(gerror);
			}
			g_free(str);

			gerror = NULL;
			str = g_key_file_get_string(config_file, groups[i], "MaxSize", &gerror);
			if (!gerror && (str != NULL)) {
				if (ParseSize(str, &(parseOutput.MaxSize))) {
					DbgPrint("read prop %s = %s\n", "MaxSize", str);
				} else {
					ErrPrint("Unrecognized format in MaxSize\n");
				}
			} else {
				ErrPrint("error: %s\n", gerror->message);
				g_error_free(gerror);
			}
			g_free(str);

			gerror = NULL;
			rots = g_key_file_get_integer(config_file, groups[i], "Rotations", &gerror);
			if (!gerror) {
				parseOutput.Rotations = rots;
				DbgPrint("read prop Rotations = %d\n", rots);
			} else {
				ErrPrint("error: %s\n", gerror->message);
				g_error_free(gerror);
			}

		    /* create new PmLogOuputConf_t object */
		    if (!MakeOutputConf(&parseOutput)) {
			    retVal = false;
			    break;
		    }
	    } else if (g_str_has_prefix(groups[i], contextPrefix)) {

		    /* CONTEXT parsing */
		    name = groups[i]+strlen(contextPrefix);
		    ParseContextInit(name, &parseContext);

		    gchar* str;
		    /* read rules */
		    for (j=0; j< PMLOG_CONTEXT_MAX_NUM_RULES; j++) {
			    mysprintf(ruleName, sizeof(ruleName), "Rule%d",j+1);
			    GError *gerror = NULL;
			    str = g_key_file_get_string(config_file,groups[i],ruleName,&gerror);
			    if (!gerror && (str != NULL)) {
				    if (!ParseContextData(&parseContext, ruleName, str)) {
					    retVal = false;
					    break;
				    }
			    } else {
				    if (gerror) {
					    g_error_free( gerror );
					    gerror = NULL;
				    }
				    break;
			    }
                g_free(str);
		    }

		    /* read ring buffer info */
		    str = g_key_file_get_string(config_file,groups[i], "BufferSize", &gerror);
		    if (!gerror && (str != NULL)) {
			    if (!ParseSize(str,&(parseContext.bufferSize))) {
				    ErrPrint("%s: Couldn't parse %s %s\n", __FUNCTION__, groups[i], "BufferSize");
				    retVal = false;
			    }
		    } else if (gerror) {
			    g_error_free( gerror );
			    gerror = NULL;
		    }
            g_free(str);

		    /* read ring buffer info */
		    str = g_key_file_get_string(config_file,groups[i], "FlushLevel", &gerror);
		    if (!gerror && (str != NULL)) {
			    if (!ParseLevel(str,&(parseContext.flushLevel))) {
				    ErrPrint("%s: Couldn't parse %s %s\n", __FUNCTION__, groups[i], "FlushLevel");
				    retVal = false;
			    }
		    } else if (gerror) {
			    g_error_free( gerror );
			    gerror = NULL;
		    }
            g_free(str);


		    /* create new PmLogContextConf_t object */
		    if (!MakeContextConf(&parseContext)) {
			    retVal = false;
			    break;
		    }
	    } else {
		    ErrPrint("%s: Unrecognized group %s\n", __FUNCTION__, groups[i]);
	    }
    }

end:
    if (groups)
        g_strfreev(groups);
    if (gerror)
	    g_error_free(gerror);
    if (config_file)
	    g_key_file_free(config_file);
    return retVal;
}


/**
 * @brief SetDefaultConf
 */
void SetDefaultConf(void)
{
	PmLogFile_t			*outputConfP;
	PmLogContextConf_t	*contextConfP;
	PmLogRule_t			*contextRuleP;

	DbgPrint("Setting default config\n");

	ClearConf();

	outputConfP = &g_outputConfs[ 0 ];

	outputConfP->outputName		= g_strdup(PMLOG_OUTPUT_STDLOG);
	outputConfP->path			= g_strdup(DEFAULT_LOG_FILE_PATH);
	outputConfP->maxSize		= PMLOG_DEFAULT_LOG_SIZE;
	outputConfP->rotations		= PMLOG_DEFAULT_LOG_ROTATIONS;

	g_numOutputs = 1;

	contextConfP = g_tree_lookup(g_contextConfs, PMLOG_CONTEXT_GLOBAL);
	if (contextConfP == NULL)
		contextConfP = CreateContext(PMLOG_CONTEXT_GLOBAL);

	contextRuleP = &contextConfP->rules[ 0 ];

	contextRuleP->facility		= -1;
	contextRuleP->level			= -1;
	contextRuleP->levelInvert	= false;
	g_free(contextRuleP->program);
    contextRuleP->program = NULL;
	contextRuleP->outputIndex	= 0;
	contextRuleP->omitOutput	= false;

	contextConfP->numRules = 1;

}
