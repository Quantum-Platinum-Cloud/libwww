/*	Configuration manager for Hypertext Daemon		HTRules.c
**	==========================================
**
**
** History:
**	 3 Jun 91	Written TBL
**	10 Aug 91	Authorisation added after Daniel Martin (pass, fail)
**			Rule order in file changed
**			Comments allowed with # on 1st char of rule line
**      17 Jun 92       Bug fix: pass and fail failed if didn't contain '*' TBL
**       1 Sep 93       Bug fix: no memory check - Nathan Torkington
**                      BYTE_ADDRESSING removed - Arthur Secret
**	11 Sep 93  MD	Changed %i into %d in debug printf. 
**			VMS does not recognize %i.
**			Bug Fix: in case of PASS, only one parameter to printf.
**	19 Sep 93  AL	Added Access Authorization stuff.
**	 1 Nov 93  AL	Added htbin.
**	30 Nov 93  AL	Added HTTranslateReq().
**
*/

/* (c) CERN WorldWideWeb project 1990,91. See Copyright.html for details */
#include "HTRules.h"

#include <stdio.h>
#include "tcp.h"
#include "HTFile.h"
#include "HTAAServ.h"	/* Access Authorization */

#define LINE_LENGTH 256


typedef struct _rule {
	struct _rule *	next;
	HTRuleOp	op;
	char *		pattern;
	char *		equiv;
} rule;

/*	Global variables (these will be obsolite once I put exec rule in)
**	----------------
*/
PUBLIC char *HTBinDir = NULL;	/* Physical /htbin directory path.	*/
                                /* In future this should not be global.	*/
PUBLIC char *HTSearchScript = NULL;	/* Search script name.		*/


/*	Module-wide variables
**	---------------------
*/

PRIVATE rule * rules = 0;	/* Pointer to first on list */
#ifndef PUT_ON_HEAD
PRIVATE rule * rule_tail = 0;	/* Pointer to last on list */
#endif


/*	Add rule to the list					HTAddRule()
**	--------------------
**
**  On entry,
**	pattern		points to 0-terminated string containing a single "*"
**	equiv		points to the equivalent string with * for the
**			place where the text matched by * goes.
**  On exit,
**	returns		0 if success, -1 if error.
*/

PUBLIC int HTAddRule ARGS3(HTRuleOp,		op,
			   CONST char *,	pattern,
			   CONST char *,	equiv)
{ /* BYTE_ADDRESSING removed and memory check - AS - 1 Sep 93 */
    rule *      temp;
    char *      pPattern;

    temp = (rule *)malloc(sizeof(*temp));
    if (temp==NULL) 
	outofmem(__FILE__, "HTAddRule"); 
    pPattern = (char *)malloc(strlen(pattern)+1);
    if (pPattern==NULL) 
	outofmem(__FILE__, "HTAddRule"); 
    if (equiv) {		/* Two operands */
	char *	pEquiv = (char *)malloc(strlen(equiv)+1);
	if (pEquiv==NULL) 
	    outofmem(__FILE__, "HTAddRule"); 
        temp->equiv = pEquiv;
        strcpy(pEquiv, equiv);
    } else {
        temp->equiv = 0;
    }
    temp->pattern = pPattern;
    temp->op = op;

    strcpy(pPattern, pattern);
    if (TRACE) {
       if (equiv)
          printf("Rule: For `%s' op %d `%s'\n", pattern, op, equiv);
       else
          printf("Rule: For `%s' op %d\n", pattern, op);
    }

#ifdef PUT_ON_HEAD
    temp->next = rules;
    rules = temp;
#else
    temp->next = 0;
    if (rule_tail) rule_tail->next = temp;
    else rules = temp;
    rule_tail = temp;
#endif

        
    return 0;
}


/*	Clear all rules						HTClearRules()
**	---------------
**
** On exit,
**	There are no rules
**	returns		0 if success, -1 if error.
**
** See also
**	HTAddRule()
*/
PUBLIC int HTClearRules NOARGS
{
    while (rules) {
    	rule * temp = rules;
	rules = temp->next;
	free(temp->pattern);
	free(temp->equiv);
	free(temp);
    }
#ifndef PUT_ON_HEAD
    rule_tail = 0;
#endif

    return 0;
}


/*	Translate by rules					HTTranslate()
**	------------------
**
** ATTENTION:
**	THIS FUNCTION HAS BEEN OBSOLITED BY HTTranslateReq()
**	ON SERVER SIDE -- ON BROWSER SIDE THIS IS STILL USED!
**	Don't add new server features to this, this already has
**	more than it can handle cleanly.
**
**	The most recently defined rules are applied last.
**
** On entry,
**	required	points to a string whose equivalent value is neeed
** On exit,
**	returns		the address of the equivalent string allocated from
**			the heap which the CALLER MUST FREE. If no translation
**			occured, then it is a copy of te original.
** NEW FEATURES:
**			When a "protect" or "defprot" rule is mathed,
**			a call to HTAA_setCurrentProtection() or
**			HTAA_setDefaultProtection() is made to notify
**			the Access Authorization module that the file is
**			protected, and so it knows how to handle it.
**								-- AL
*/
PUBLIC char * HTTranslate ARGS1(CONST char *, required)
{
    rule * r;
    char *current = NULL;
    StrAllocCopy(current, required);

#ifdef OLD_CODE
    HTAA_clearProtections();	/* Reset from previous call -- AL */
#endif

    for(r = rules; r; r = r->next) {
        char * p = r->pattern;
	int m=0;   /* Number of characters matched against wildcard */
	CONST char * q = current;
	for(;*p && *q; p++, q++) {   /* Find first mismatch */
	    if (*p!=*q) break;
	}

	if (*p == '*') {		/* Match up to wildcard */
	    m = strlen(q) - strlen(p+1); /* Amount to match to wildcard */
	    if(m<0) continue;           /* tail is too short to match */
	    if (0!=strcmp(q+m, p+1)) continue;	/* Tail mismatch */
	} else 				/* Not wildcard */
	    if (*p != *q) continue;	/* plain mismatch: go to next rule */

	switch (r->op) {		/* Perform operation */

#ifdef ACCESS_AUTH
	case HT_DefProt:
	case HT_Protect:
	    {
		char *local_copy = NULL;
		char *p;
		char *eff_ids = NULL;
		char *prot_file = NULL;

		if (TRACE) fprintf(stderr,
				   "HTRule: `%s' matched %s %s: `%s'\n",
				   current,
				   (r->op==HT_Protect ? "Protect" : "DefProt"),
				   "rule, setup",
				   (r->equiv ? r->equiv :
				    (r->op==HT_Protect ?"DEFAULT" :"NULL!!")));

		if (r->equiv) {
		    StrAllocCopy(local_copy, r->equiv);
		    p = local_copy;
		    prot_file = HTNextField(&p);
		    eff_ids = HTNextField(&p);
		}

#ifdef THESE_NO_LONGER_WORK
		if (r->op == HT_Protect)
		    HTAA_setCurrentProtection(current, prot_file, eff_ids);
		else
		    HTAA_setDefaultProtection(current, prot_file, eff_ids);
#endif
		FREE(local_copy);

		/* continue translating rules */
	    }
	    break;
#endif ACCESS_AUTH

	case HT_Pass:				/* Authorised */
    		if (!r->equiv) {
		    if (TRACE) printf("HTRule: Pass `%s'\n", current);
		    return current;
	        }
		/* Else fall through ...to map and pass */
		
	case HT_Map:
	    if (*p == *q) { /* End of both strings, no wildcard */
    	          if (TRACE) printf(
			       "For `%s' using `%s'\n", current, r->equiv);  
	          StrAllocCopy(current, r->equiv); /* use entire translation */
	    } else {
		  char * ins = strchr(r->equiv, '*');	/* Insertion point */
	          if (ins) {	/* Consistent rule!!! */
			char * temp = (char *)malloc(
				strlen(r->equiv)-1 + m + 1);
			if (temp==NULL) 
			    outofmem(__FILE__, "HTTranslate"); /* NT & AS */
			strncpy(temp, 	r->equiv, ins-r->equiv);
			/* Note: temp may be unterminated now! */
			strncpy(temp+(ins-r->equiv), q, m);  /* Matched bit */
			strcpy (temp+(ins-r->equiv)+m, ins+1);	/* Last bit */
    			if (TRACE) printf("For `%s' using `%s'\n",
						current, temp);
			free(current);
			current = temp;			/* Use this */

		    } else {	/* No insertion point */
			char * temp = (char *)malloc(strlen(r->equiv)+1);
			if (temp==NULL) 
			    outofmem(__FILE__, "HTTranslate"); /* NT & AS */
			strcpy(temp, r->equiv);
    			if (TRACE) printf("For `%s' using `%s'\n",
						current, temp);
			free(current);
			current = temp;			/* Use this */
		    } /* If no insertion point exists */
		}
		if (r->op == HT_Pass) {
		    if (TRACE) printf("HTRule: ...and pass `%s'\n", current);
		    return current;
		}
		break;

	case HT_Exec:
	case HT_Invalid:
	case HT_Fail:				/* Unauthorised */
	default:
    		    if (TRACE) printf("HTRule: *** FAIL `%s'\n", current);
		    return (char *)0;
		    		    
	} /* if tail matches ... switch operation */

    } /* loop over rules */


    return current;
}



/*	Translate by rules					HTTranslate()
**	------------------
**
** On entry,
**	req		request structure.
**	req->simplified	simplified pathname (no ..'s etc in it),
**			which will be translated.
**			If this starts with /htbin/ it is taken
**			to be a script call request.
**
** On exit,
**	returns		YES on success, NO on failure (Forbidden).
**	req->translated	contains the translated filename;
**			NULL if a script call.
**	req->script	contains the executable script name;
**			NULL if not a script call.
*/
PUBLIC BOOL HTTranslateReq ARGS1(HTRequest *, req)
{
    rule * r;
    char *current = NULL;

    if (!req  ||  !req->simplified)
	return NO;

    current = strdup(req->simplified);

#ifdef OLD_CODE
    if (0 == strncmp(current, "/htbin/", 7)) {
	if (!HTBinDir) {
	    req->reason = HTAA_HTBIN;
	    return NO;
	}
	else {
	    char *end = strchr(current + 7, '/');
	    if (end)
		*end = (char)0;
	    req->script=(char*)malloc(strlen(HTBinDir)+strlen(current)+1);
	    strcpy(req->script, HTBinDir);
	    strcat(req->script, current + 6);
	    if (end) {
		*end = '/';	/* Reconstruct */
		req->script_pathinfo = strdup(end);	/* @@@@ This should */
		                                        /* be translated !! */
	    }
	    free(current);
	    return YES;
	}
    }
#endif /*OLD_CODE*/

    for(r = rules; r; r = r->next) {
        char * p = r->pattern;
	int m=0;   /* Number of characters matched against wildcard */
	CONST char * q = current;
	for(;*p && *q; p++, q++) {   /* Find first mismatch */
	    if (*p!=*q) break;
	}

	if (*p == '*') {		/* Match up to wildcard */
	    m = strlen(q) - strlen(p+1); /* Amount to match to wildcard */
	    if(m<0) continue;           /* tail is too short to match */
	    if (0!=strcmp(q+m, p+1)) continue;	/* Tail mismatch */
	} else 				/* Not wildcard */
	    if (*p != *q) continue;	/* plain mismatch: go to next rule */

	switch (r->op) {		/* Perform operation */

#ifdef ACCESS_AUTH
	case HT_DefProt:
	case HT_Protect:
	    {
		char *local_copy = NULL;
		char *p;
		char *eff_ids = NULL;
		char *prot_file = NULL;

		if (TRACE) fprintf(stderr,
				   "HTRule: `%s' matched %s %s: `%s'\n",
				   current,
				   (r->op==HT_Protect ? "Protect" : "DefProt"),
				   "rule, setup",
				   (r->equiv ? r->equiv :
				    (r->op==HT_Protect ?"DEFAULT" :"NULL!!")));

		if (r->equiv) {
		    StrAllocCopy(local_copy, r->equiv);
		    p = local_copy;
		    prot_file = HTNextField(&p);
		    eff_ids = HTNextField(&p);
		}

		if (r->op == HT_Protect)
		    HTAA_setCurrentProtection(req, prot_file, eff_ids);
		else
		    HTAA_setDefaultProtection(req, prot_file, eff_ids);

		FREE(local_copy);

		/* continue translating rules */
	    }
	    break;
#endif ACCESS_AUTH

	case HT_Exec:
	    if (!r->equiv) {
		if (TRACE) fprintf(stderr,
				   "HTRule: Exec `%s', no extra pathinfo\n",
				   current);
		req->script = current;
		req->script_pathinfo = NULL;
		return YES;
	    }
	    else if (*p == *q || !strchr(r->equiv, '*')) { /* No wildcards */
		if (TRACE) fprintf(stderr,
				   "HTRule: Exec `%s', no extra pathinfo\n",
				   r->equiv);
		StrAllocCopy(req->script, r->equiv);
		req->script_pathinfo = NULL;
		return YES;
	    }
	    else {
		char *ins = strchr(r->equiv, '*');
		char *pathinfo;
		if (!(req->script = (char*)malloc(strlen(r->equiv) + m)))
		    outofmem(__FILE__, "HTTranslate");
		strncpy(req->script, r->equiv, ins-r->equiv);
		strncpy(req->script+(ins-r->equiv), q, m);
		strcpy(req->script+(ins-r->equiv)+m, ins+1);
		for (pathinfo = req->script+(ins-r->equiv)+1;
		     *pathinfo && *pathinfo != '/';
		     pathinfo++)
		    ;
		if (*pathinfo) {
		    StrAllocCopy(req->script_pathinfo, pathinfo);
		    *pathinfo = 0;
		}
		return YES;
	    }
	    break;
			     
	case HT_Pass:				/* Authorised */
    		if (!r->equiv) {
		    if (TRACE) fprintf(stderr, "HTRule: Pass `%s'\n", current);
		    req->translated = current;
		    return YES;
	        }
		/* Else fall through ...to map and pass */
		
	case HT_Map:
	    if (*p == *q) { /* End of both strings, no wildcard */
    	          if (TRACE) printf(
			       "For `%s' using `%s'\n", current, r->equiv);  
	          StrAllocCopy(current, r->equiv); /* use entire translation */
	    } else {
		  char * ins = strchr(r->equiv, '*');	/* Insertion point */
	          if (ins) {	/* Consistent rule!!! */
			char * temp = (char *)malloc(
				strlen(r->equiv)-1 + m + 1);
			if (temp==NULL) 
			    outofmem(__FILE__, "HTTranslate"); /* NT & AS */
			strncpy(temp, 	r->equiv, ins-r->equiv);
			/* Note: temp may be unterminated now! */
			strncpy(temp+(ins-r->equiv), q, m);  /* Matched bit */
			strcpy (temp+(ins-r->equiv)+m, ins+1);	/* Last bit */
    			if (TRACE) printf("For `%s' using `%s'\n",
						current, temp);
			free(current);
			current = temp;			/* Use this */

		    } else {	/* No insertion point */
			char * temp = (char *)malloc(strlen(r->equiv)+1);
			if (temp==NULL) 
			    outofmem(__FILE__, "HTTranslate"); /* NT & AS */
			strcpy(temp, r->equiv);
    			if (TRACE) printf("For `%s' using `%s'\n",
						current, temp);
			free(current);
			current = temp;			/* Use this */
		    } /* If no insertion point exists */
		}
		if (r->op == HT_Pass) {
		    if (TRACE) fprintf(stderr, "HTRule: Pass `%s'\n", current);
		    req->translated = current;
		    return YES;
		}
		break;

	case HT_Invalid:
	case HT_Fail:				/* Unauthorised */
    		    if (TRACE) printf("HTRule: *** FAIL `%s'\n", current);
		    return NO;
	            break;
	} /* if tail matches ... switch operation */

    } /* loop over rules */

    /* Actually here failing might be more appropriate?? */
    req->translated = current;
    return YES;
}



/*	Load one line of configuration
**	------------------------------
**
**	Call this, for example, to load a X resource with config info.
**
** returns	0 OK, < 0 syntax error.
*/
PUBLIC int HTSetConfiguration ARGS1(CONST char *, config)
{
    HTRuleOp op;
    char * line = NULL;
    char * pointer = line;
    char *word1, *word2, *word3;
    float quality, secs, secs_per_byte;
    int status;
    
    StrAllocCopy(line, config);
    {
	char * p = strchr(line, '#');	/* Chop off comments */
	if (p) *p = 0;
    }
    pointer = line;
    word1 = HTNextField(&pointer);
    if (!word1) {
    	free(line);
	return 0;
    } ;	/* Comment only or blank */

    word2 = HTNextField(&pointer);

    if (0==strcasecomp(word1, "defprot") ||
	0==strcasecomp(word1, "protect"))
	word3 = pointer;  /* The rest of the line to be parsed by AA module */
    else
	word3 = HTNextField(&pointer);	/* Just the next word */

    if (!word2) {
	fprintf(stderr, "HTRule: Insufficient operands: %s\n", line);
	free(line);
	return -2;	/*syntax error */
    }

    if (0==strcasecomp(word1, "suffix")) {
        char * encoding = HTNextField(&pointer);
	if (pointer) status = sscanf(pointer, "%f", &quality);
	else status = 0;
	HTSetSuffix(word2,	word3,
				encoding ? encoding : "binary",
				status >= 1? quality : 1.0);

    } else if (0==strcasecomp(word1, "presentation")) {
        if (pointer) status = sscanf(pointer, "%f%f%f",
			    &quality, &secs, &secs_per_byte);
        else status = 0;
	if (!HTConversions) HTConversions = HTList_new();
	HTSetPresentation(HTConversions, word2, word3,
		    status >= 1? quality 		: 1.0,
		    status >= 2 ? secs 			: 0.0,
		    status >= 3 ? secs_per_byte 	: 0.0 );

    } else if (0==strncasecomp(word1, "htbin", 5) ||
	       0==strncasecomp(word1, "bindir", 6)) {
	char *bindir = (char*)malloc(strlen(word2) + 3);
	if (!bindir) outofmem(__FILE__, "HTSetConfiguration");
	strcpy(bindir, word2);
	strcat(bindir, "/*");
	HTAddRule(HT_Exec, "/htbin/*", bindir);

	/*
	** Physical /htbin location -- this is almost obsolite
	** (only search may need it).
	*/
	StrAllocCopy(HTBinDir, word2);

    } else if (0==strncasecomp(word1, "search", 6)) {
	if (strchr(word2, '/'))
	    StrAllocCopy(HTSearchScript, word2); /* Full search script path */
	else if (HTBinDir) {
	    if (!(HTSearchScript =
		  (char*)malloc(strlen(HTBinDir) + strlen(word2) + 2)))
		outofmem(__FILE__, "HTSetConfiguration");
	    strcpy(HTSearchScript, HTBinDir);
	    strcat(HTSearchScript, "/");
	    strcat(HTSearchScript, word2);
	}
	else if (TRACE) fprintf(stderr,
		"HTRule: Search rule without HTBin rule before ignored\n");
	if (TRACE) {
	    if (HTSearchScript)
		fprintf(stderr, "HTRule: Search script set to `%s'\n",
			HTSearchScript);
	    else fprintf(stderr, "HTRule: Search script not set\n");
	}

    } else {
	op =	0==strcasecomp(word1, "map")  ?	HT_Map
	    :	0==strcasecomp(word1, "pass") ?	HT_Pass
	    :	0==strcasecomp(word1, "fail") ?	HT_Fail
	    :   0==strcasecomp(word1, "defprot") ? HT_DefProt
	    :	0==strcasecomp(word1, "protect") ? HT_Protect
	    :						HT_Invalid;
	if (op==HT_Invalid) {
	    fprintf(stderr, "HTRule: Bad rule `%s'\n", config);
	} else {  
	    HTAddRule(op, word2, word3);
	} 
    }
    free(line);
    return 0;
}


/*	Load the rules from a file				HTLoadRules()
**	--------------------------
**
** On entry,
**	Rules can be in any state
** On exit,
**	Any existing rules will have been kept.
**	Any new rules will have been loaded.
**	Returns		0 if no error, 0 if error!
**
** Bugs:
**	The strings may not contain spaces.
*/

int HTLoadRules ARGS1(CONST char *, filename)
{
    FILE * fp = fopen(filename, "r");
    char line[LINE_LENGTH+1];
    
    if (!fp) {
        if (TRACE) printf("HTRules: Can't open rules file %s\n", filename);
	return -1; /* File open error */
    }
    for(;;) {
	if (!fgets(line, LINE_LENGTH+1, fp)) break;	/* EOF or error */
	(void) HTSetConfiguration(line);
    }
    fclose(fp);
    return 0;		/* No error or syntax errors ignored */
}


