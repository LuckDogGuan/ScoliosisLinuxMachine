#ifndef INSTRUMENT_EVIDENCE_H
#define INSTRUMENT_EVIDENCE_H
#include "v3.h"
#define INSTRUMENT_LOG_DIR INSTRUMENT_ROOT "/logs"
cJSON *evidence_build(const Config *config,const cJSON *snapshot,const char *fault,const char *capture,const char *boot,double utc);
int evidence_store(const cJSON *body);
int evidence_send(const Config *config,const cJSON *body,const char *token);
#endif
