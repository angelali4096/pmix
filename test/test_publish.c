/*
 * Copyright (c) 2015      Intel, Inc.  All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 *
 */

#include "test_publish.h"
#include <time.h>
#include "src/buffer_ops/buffer_ops.h"

typedef struct {
    int in_progress;
    size_t ninfo;
    pmix_info_t *info;
} lookup_cbdata;

static void release_cb(pmix_status_t status, void *cbdata)
{
    int *ptr = (int*)cbdata;
    *ptr = 0;
}

static void lookup_cb(pmix_status_t status,
                      pmix_info_t info[], size_t ninfo,
                      char nspace[], void *cbdata)
{
    size_t i, j;
    lookup_cbdata *cb = (lookup_cbdata*)cbdata;
    pmix_info_t *tgt = cb->info;

    /* find the matching key in the provided info array - error if not found */
    for (i=0; i < ninfo; i++) {
        for (j=0; j < cb->ninfo; j++) {
            if (0 == strcmp(info[i].key, tgt[j].key)) {
                /* transfer the value to the pmix_info_t */
                pmix_value_xfer(&tgt[j].value, &info[i].value);
                break;
            }
        }
    }
    cb->in_progress = 0;
}

static int test_publish(char *my_nspace, int my_rank, int blocking)
{
    int rc;
    pmix_info_t info;
    char data[512];

    PMIX_INFO_CONSTRUCT(&info);
    (void)snprintf(info.key, PMIX_MAX_KEYLEN, "%s:%d", my_nspace, my_rank);
    (void)snprintf(data, 512, "data from proc %s:%d", my_nspace, my_rank);
    info.value.type = PMIX_STRING;
    info.value.data.string = strdup(data);
    if (blocking) {
        rc = PMIx_Publish(PMIX_UNIVERSAL, PMIX_PERSIST_INDEF, &info, 1);
    } else {
        int in_progress = 1, count;
        rc = PMIx_Publish_nb(PMIX_UNIVERSAL, PMIX_PERSIST_INDEF, &info, 1, release_cb, &in_progress);
        if (PMIX_SUCCESS == rc) {
            count = 0;
            while( in_progress ){
                struct timespec ts;
                ts.tv_sec = 0;
                ts.tv_nsec = 100;
                nanosleep(&ts,NULL);
                count++;
            }
        }
    }
    PMIX_INFO_DESTRUCT(&info);
    return rc;
}

static int test_lookup(char *my_nspace, int my_rank, int blocking)
{
    int rc;
    pmix_info_t info;
    char data[512];
    char *keys[2];

    PMIX_INFO_CONSTRUCT(&info);
    (void)snprintf(info.key, PMIX_MAX_KEYLEN, "%s:%d", my_nspace, my_rank);
    (void)snprintf(data, 512, "data from proc %s:%d", my_nspace, my_rank);

    if (blocking) {
        if (PMIX_SUCCESS != (rc = PMIx_Lookup(PMIX_UNIVERSAL, &info, 1, NULL))) {
            PMIX_INFO_DESTRUCT(&info);
            return rc;
        }
    } else {
        keys[0] = (char*)malloc(PMIX_MAX_KEYLEN * sizeof(char));
        (void)snprintf(keys[0], PMIX_MAX_KEYLEN, "%s:%d", my_nspace, my_rank);
        keys[1] = NULL;

        int count;
        lookup_cbdata cbdata;
        cbdata.in_progress = 1;
        cbdata.ninfo = 1;
        cbdata.info = &info;
        rc = PMIx_Lookup_nb(PMIX_UNIVERSAL, 1, keys, lookup_cb, (void*)&cbdata);
        if (PMIX_SUCCESS != rc) {
            PMIX_INFO_DESTRUCT(&info);
            return rc;
        }
        count = 0;
        while(cbdata.in_progress){
            struct timespec ts;
            ts.tv_sec = 0;
            ts.tv_nsec = 100;
            nanosleep(&ts,NULL);
            count++;
        }
    }

    if (PMIX_STRING != info.value.type ||
            NULL == info.value.data.string) {
        PMIX_INFO_DESTRUCT(&info);
        return PMIX_ERR_NOT_FOUND;
    }

    if (strncmp(data, info.value.data.string, strlen(data))) {
        PMIX_INFO_DESTRUCT(&info);
        return PMIX_ERR_NOT_FOUND;
    }
    PMIX_INFO_DESTRUCT(&info);
    return rc;
}

static int test_unpublish(char *my_nspace, int my_rank, int blocking)
{
    int rc;
    char *keys[2];

    keys[0] = (char*)malloc(PMIX_MAX_KEYLEN * sizeof(char));
    (void)snprintf(keys[0], PMIX_MAX_KEYLEN, "%s:%d", my_nspace, my_rank);
    keys[1] = NULL;

    if (blocking) {
        rc = PMIx_Unpublish(PMIX_UNIVERSAL, keys);
    } else {
        int in_progress = 1, count;
        rc = PMIx_Unpublish_nb(PMIX_UNIVERSAL, keys, release_cb, &in_progress);
        if (PMIX_SUCCESS == rc) {
            count = 0;
            while( in_progress ){
                struct timespec ts;
                ts.tv_sec = 0;
                ts.tv_nsec = 100;
                nanosleep(&ts,NULL);
                count++;
            }
        }
    }
    free(keys[0]);
    return rc;
}

static int test_publish_lookup_common(char *my_nspace, int my_rank, int blocking)
{
    int rc;
    rc = test_publish(my_nspace, my_rank, blocking);
    if (PMIX_SUCCESS != rc) {
        TEST_ERROR(("%s:%d: %s failed.", my_nspace, my_rank, blocking ? "PMIX_Publish" : "PMIX_Publish_nb"));
        return PMIX_ERROR;
    }
    TEST_VERBOSE(("%s:%d: %s succeeded.", my_nspace, my_rank, blocking ? "PMIX_Publish" : "PMIX_Publish_nb"));

    rc = test_lookup(my_nspace, my_rank, blocking);
    if (PMIX_SUCCESS != rc) {
        TEST_ERROR(("%s:%d: %s failed.", my_nspace, my_rank, blocking ? "PMIX_Lookup" : "PMIX_Lookup_nb"));
        return PMIX_ERROR;
    }
    TEST_VERBOSE(("%s:%d: %s succeeded.\n", my_nspace, my_rank, blocking ? "PMIX_Lookup" : "PMIX_Lookup_nb"));

    rc = test_unpublish(my_nspace, my_rank, blocking);
    if (PMIX_SUCCESS != rc) {
        TEST_ERROR(("%s:%d: %s failed.", my_nspace, my_rank, blocking ? "PMIX_Unpublish" : "PMIX_Unpublish_nb"));
        return PMIX_ERROR;
    }
    TEST_VERBOSE(("%s:%d: %s succeeded.", my_nspace, my_rank, blocking ? "PMIX_Unpublish" : "PMIX_Unpublish_nb"));

    rc = test_lookup(my_nspace, my_rank, blocking);
    if (PMIX_ERR_NOT_FOUND != rc) {
        TEST_ERROR(("%s:%d: %s function returned %d instead of PMIX_ERR_NOT_FOUND.", my_nspace, my_rank, blocking ? "PMIX_Lookup" : "PMIX_Lookup_nb", rc));
        return PMIX_ERROR;
    }
    return PMIX_SUCCESS;
}

int test_publish_lookup(char *my_nspace, int my_rank)
{
    int rc;
    /* test blocking */
    rc = test_publish_lookup_common(my_nspace, my_rank, 1);
    if (PMIX_SUCCESS != rc) {
        TEST_ERROR(("%s:%d: Publish/Lookup blocking test failed.", my_nspace, my_rank));
        return PMIX_ERROR;
    }
    /* test non-blocking */
    rc = test_publish_lookup_common(my_nspace, my_rank, 0);
    if (PMIX_SUCCESS != rc) {
        TEST_ERROR(("%s:%d: Publish/Lookup non-blocking test failed.", my_nspace, my_rank));
        return PMIX_ERROR;
    }
    return PMIX_SUCCESS;
}
