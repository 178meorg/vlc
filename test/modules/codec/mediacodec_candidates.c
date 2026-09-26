/* Copyright (C) 2026 VLC authors and VideoLAN
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#undef NDEBUG
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static int allocation_budget = -1;

static bool FailAllocation(void)
{
    if (allocation_budget < 0)
        return false;
    if (allocation_budget == 0)
        return true;
    allocation_budget--;
    return false;
}

static void *TestMalloc(size_t size)
{
    return FailAllocation() ? NULL : malloc(size);
}

static void *TestRealloc(void *ptr, size_t size)
{
    return FailAllocation() ? NULL : realloc(ptr, size);
}

static char *TestStrdup(const char *text)
{
    return FailAllocation() ? NULL : strdup(text);
}

#define malloc TestMalloc
#define realloc TestRealloc
#define strdup TestStrdup
#include "../../../modules/codec/omxil/mediacodec_candidates.h"
#undef malloc
#undef realloc
#undef strdup

static void TestScores(void)
{
    struct mc_score_rule *rules;
    unsigned invalid;
    assert(mc_score_rules_parse(NULL, &rules, &invalid) == 0);
    assert(rules == NULL && invalid == 0);
    assert(mc_candidate_score(rules, "OMX.vendor.hevc") == 100);

    assert(mc_score_rules_parse(
        " ^OMX\\.vendor\\.hevc$ = 100, ^OMX\\.vendor\\. = 20,"
        "^OMX\\.google\\.=-10,^c2\\.=+5", &rules, &invalid) == 0);
    assert(invalid == 0);
    assert(mc_candidate_score(rules, "OMX.vendor.hevc") == 100);
    assert(mc_candidate_score(rules, "OMX.vendor.avc") == 20);
    assert(mc_candidate_score(rules, "OMX.google.hevc") == -10);
    assert(mc_candidate_score(rules, "c2.vendor.hevc") == 5);
    assert(mc_candidate_score(rules, "omx.vendor.hevc") == 100);
    mc_score_rules_clear(rules);

    /* Invalid regexes/numbers do not prevent subsequent rules from working. */
    assert(mc_score_rules_parse(
        "[=3,x=,y=no,z=999999999999999999999999999999999,"
        "=5,bare,x=2x,^valid$=7", &rules, &invalid) == 0);
    assert(invalid == 7);
    assert(mc_candidate_score(rules, "valid") == 7);
    assert(mc_candidate_score(rules, "x") == 100);
    mc_score_rules_clear(rules);

    assert(mc_score_rules_parse(".*=-2147483648,^x$=2147483647",
                                &rules, &invalid) == 0);
    assert(invalid == 0);
    /* First matching rule wins, not the largest matching score. */
    assert(mc_candidate_score(rules, "x") == INT_MIN);
    mc_score_rules_clear(rules);
}

static void TestOrdering(void)
{
    struct mc_candidates list = { 0 };
    assert(mc_candidates_add(&list, "first", "video/hevc", 0, 1) == 0);
    assert(mc_candidates_add(&list, "second", "video/hevc", 0, 2) == 0);
    assert(mc_candidates_add(&list, "last", "video/hevc", INT_MIN, 3) == 0);
    assert(mc_candidates_add(&list, "preferred", "video/hevc", INT_MAX, 4) == 0);
    assert(mc_candidates_add(&list, "second", "video/hevc", 999, 99) == 0);
    assert(list.count == 4);
    assert(strcmp(list.data[0].name, "preferred") == 0);
    assert(strcmp(list.data[1].name, "second") == 0);
    assert(strcmp(list.data[2].name, "first") == 0);
    assert(strcmp(list.data[3].name, "last") == 0);
    assert(list.data[1].score == 999 && list.data[1].quirks == 99);
    mc_candidates_clear(&list);
    assert(list.count == 0 && list.data == NULL);
    mc_candidates_clear(&list);
}

static void TestFailureSet(void)
{
    const char *dolby = "video/dolby-vision", *hevc = "video/hevc";
    char *failed = mc_candidate_fail(NULL, dolby, "vendor.dual");
    assert(failed != NULL);
    assert(mc_candidate_failed(failed, dolby, "vendor.dual"));
    assert(!mc_candidate_failed(failed, hevc, "vendor.dual"));
    assert(!mc_candidate_failed(failed, dolby, "vendor.dual.extra"));
    assert(!mc_candidate_failed(failed, dolby, "vendor"));
    char *next = mc_candidate_fail(failed, dolby, "vendor.dual");
    assert(next != NULL && strcmp(next, failed) == 0);
    free(next);
    next = mc_candidate_fail(failed, hevc, "vendor.hevc");
    assert(next != NULL);
    free(failed);
    failed = next;
    /* A copied string models state surviving module teardown/reload. */
    next = strdup(failed);
    assert(next != NULL);
    free(failed);
    assert(mc_candidate_failed(next, dolby, "vendor.dual"));
    assert(mc_candidate_failed(next, hevc, "vendor.hevc"));
    assert(!mc_candidate_failed(NULL, dolby, "vendor.dual"));
    assert(!mc_candidate_failed("incomplete", hevc, "vendor.hevc"));
    free(next);
}

struct mock_component
{
    const char *name;
    const char *mime;
    int profile;
    int quirks;
    bool blacklisted;
};

struct mock_enumeration
{
    const struct mock_component *components;
    size_t count;
    const struct mc_score_rule *rules;
    const char *failed;
    unsigned calls;
    unsigned fail_call;
};

/* Replace only device enumeration, exercising the production selection and
 * exclusion helpers for both passes and every reload. */
static int MockEnumerate(void *opaque, const struct mc_candidate_request *request,
                         struct mc_candidates *list)
{
    struct mock_enumeration *ctx = opaque;
    if (++ctx->calls == ctx->fail_call)
        return -77;
    for (size_t i = 0; i < ctx->count; i++)
    {
        const struct mock_component *component = &ctx->components[i];
        if (strcmp(request->mime, component->mime) != 0 ||
            (request->profile > 0 && request->profile != component->profile))
            continue;
        bool required = request->required_mime == NULL;
        for (size_t j = 0; j < ctx->count && !required; j++)
            required = strcmp(component->name, ctx->components[j].name) == 0 &&
                       strcmp(request->required_mime, ctx->components[j].mime) == 0;
        if (required && mc_candidates_offer(list, request, ctx->rules, ctx->failed,
                         component->name, component->blacklisted,
                         component->quirks) != 0)
            return -2;
    }
    return 0;
}

#define DOLBY "video/dolby-vision"
#define HEVC "video/hevc"
static const struct mock_component mixed_components[] = {
    { "dv_only", DOLBY, -1, 1, false },
    { "dual", DOLBY, -1, 2, false },
    { "dual", HEVC, 2, 3, false },
    { "base", HEVC, 2, 4, false },
    { "dual", DOLBY, -1, 99, false }, /* duplicate: retain original quirks */
    { "blocked", DOLBY, -1, 5, true },
    { "blocked", HEVC, 2, 6, true },
};

static void TestMixedSelection(void)
{
    struct mc_candidate_request requests[3];
    size_t count = mc_candidate_requests(requests, DOLBY, -1, HEVC, 2);
    assert(count == 3);
    struct mock_enumeration ctx = {
        .components = mixed_components,
        .count = sizeof(mixed_components) / sizeof(mixed_components[0]),
    };
    struct mc_candidate result;
    bool relaxed;
    char *failed = NULL;
    const char *expected_names[] = { "dual", "dv_only", "dual", "base" };
    const char *expected_mimes[] = { DOLBY, DOLBY, HEVC, HEVC };
    const int expected_quirks[] = { 2, 1, 3, 4 };
    const int expected_scores[] = { 600, 500, 100, 100 };
    for (unsigned i = 0; i < 4; i++)
    {
        ctx.failed = failed;
        ctx.calls = 0;
        assert(mc_candidates_select(requests, count, true, MockEnumerate, &ctx,
                                     &result, &relaxed) == 0);
        assert(!relaxed && ctx.calls == 3);
        assert(strcmp(result.name, expected_names[i]) == 0);
        assert(strcmp(result.mime, expected_mimes[i]) == 0);
        assert(result.quirks == expected_quirks[i]);
        assert(result.score == expected_scores[i]);
        char *next = mc_candidate_fail(failed, result.mime, result.name);
        assert(next != NULL);
        free(failed);
        failed = next;
        free(result.name);
    }
    ctx.failed = failed;
    ctx.calls = 0;
    assert(mc_candidates_select(requests, count, true, MockEnumerate, &ctx,
                                 &result, &relaxed) == 1);
    assert(relaxed && ctx.calls == 6 && result.name == NULL);
    free(failed);
    ctx.failed = NULL;

    struct mc_score_rule *rules;
    unsigned invalid;
    assert(mc_score_rules_parse("^base$=900,^blocked$=2147483647",
                                &rules, &invalid) == 0 && invalid == 0);
    ctx.rules = rules;
    assert(mc_candidates_select(requests, count, true, MockEnumerate, &ctx,
                                 &result, &relaxed) == 0);
    assert(strcmp(result.name, "base") == 0 && strcmp(result.mime, HEVC) == 0);
    free(result.name);
    mc_score_rules_clear(rules);

    /* A rule scoring 50 ranks below the unmatched default of 100. */
    struct mc_candidates list = { 0 };
    struct mc_candidate_request base_request = { HEVC, 2, 0, NULL };
    assert(mc_score_rules_parse("^custom$=50", &rules, &invalid) == 0);
    assert(mc_candidates_offer(&list, &base_request, rules, NULL,
                                "custom", false, 1) == 0);
    assert(mc_candidates_offer(&list, &base_request, rules, NULL,
                                "default", false, 2) == 0);
    assert(list.count == 2 && strcmp(list.data[0].name, "default") == 0);
    mc_candidates_clear(&list);
    mc_score_rules_clear(rules);

    /* Equal totals retain group order, even across different MIME types. */
    assert(mc_score_rules_parse("^base$=600,^dv_only$=200", &rules, &invalid) == 0);
    ctx.rules = rules;
    assert(mc_candidates_select(requests, count, true, MockEnumerate, &ctx,
                                 &result, &relaxed) == 0);
    assert(strcmp(result.name, "dual") == 0 && strcmp(result.mime, DOLBY) == 0);
    free(result.name);
    mc_score_rules_clear(rules);

    /* A negative name score must be rejected before adding 500 or 400. */
    assert(mc_score_rules_parse("^dual$=-1,^dv_only$=-2147483648",
                                &rules, &invalid) == 0);
    ctx.rules = rules;
    assert(mc_candidates_select(requests, count, true, MockEnumerate, &ctx,
                                 &result, &relaxed) == 0);
    assert(strcmp(result.name, "base") == 0);
    free(result.name);
    mc_score_rules_clear(rules);

    assert(mc_score_rules_parse("^dual$=2147483647", &rules, &invalid) == 0);
    ctx.rules = rules;
    assert(mc_candidates_select(requests, count, true, MockEnumerate, &ctx,
                                 &result, &relaxed) == 0);
    assert(result.score == (int64_t)INT_MAX + 500);
    free(result.name);
    mc_score_rules_clear(rules);
    ctx.rules = NULL;

    /* Retry disabled: one MIME, no base-layer candidate even when DV fails. */
    count = mc_candidate_requests(requests, DOLBY, -1, NULL, 2);
    assert(count == 1);
    failed = mc_candidate_fail(NULL, DOLBY, "dv_only");
    assert(failed != NULL);
    ctx.failed = mc_candidate_fail(failed, DOLBY, "dual");
    assert(ctx.failed != NULL);
    free(failed);
    ctx.calls = 0;
    assert(mc_candidates_select(requests, count, true, MockEnumerate, &ctx,
                                 &result, &relaxed) == 1);
    assert(!relaxed && ctx.calls == 1);
    free((char *)ctx.failed);
}

static void TestProfileRelaxation(void)
{
    const struct mock_component components[] = {
        { "underreported", HEVC, 1, 7, false },
        { "compatible", HEVC, 2, 8, false },
        { "dolby", DOLBY, -1, 9, false },
    };
    struct mock_enumeration ctx = { .components = components, .count = 2 };
    struct mc_candidate_request requests[3];
    size_t count = mc_candidate_requests(requests, HEVC, 2, NULL, 0);
    struct mc_candidate result;
    bool relaxed;
    assert(mc_candidates_select(requests, count, true, MockEnumerate, &ctx,
                                 &result, &relaxed) == 0);
    assert(!relaxed && ctx.calls == 1);
    assert(strcmp(result.name, "compatible") == 0 && result.quirks == 8);
    free(result.name);

    ctx.count = 1;
    ctx.calls = 0;
    assert(mc_candidates_select(requests, count, true, MockEnumerate, &ctx,
                                 &result, &relaxed) == 0);
    assert(relaxed && ctx.calls == 2 && requests[0].profile == 2);
    assert(strcmp(result.name, "underreported") == 0 && result.quirks == 7);
    free(result.name);
    ctx.calls = 0;
    assert(mc_candidates_select(requests, count, false, MockEnumerate, &ctx,
                                 &result, &relaxed) == 1);
    assert(!relaxed && ctx.calls == 1 && result.name == NULL);

    /* Negative scores and prior failures still apply after relaxing profiles. */
    struct mc_score_rule *rules;
    unsigned invalid;
    assert(mc_score_rules_parse("underreported=-1", &rules, &invalid) == 0);
    ctx.rules = rules;
    assert(mc_candidates_select(requests, count, true, MockEnumerate, &ctx,
                                 &result, &relaxed) == 1);
    assert(relaxed && result.name == NULL);
    mc_score_rules_clear(rules);
    ctx.rules = NULL;
    ctx.failed = mc_candidate_fail(NULL, HEVC, "underreported");
    assert(ctx.failed != NULL);
    assert(mc_candidates_select(requests, count, true, MockEnumerate, &ctx,
                                 &result, &relaxed) == 1);
    assert(relaxed && result.name == NULL);
    free((char *)ctx.failed);

    /* Relax only when the WHOLE strict pool is empty, not each MIME alone. */
    ctx.count = 3;
    ctx.failed = mc_candidate_fail(NULL, HEVC, "compatible");
    assert(ctx.failed != NULL);
    ctx.calls = 0;
    count = mc_candidate_requests(requests, DOLBY, -1, HEVC, 2);
    assert(mc_candidates_select(requests, count, true, MockEnumerate, &ctx,
                                 &result, &relaxed) == 0);
    assert(!relaxed && ctx.calls == 3 && strcmp(result.mime, DOLBY) == 0);
    free(result.name);
    char *failed = mc_candidate_fail(ctx.failed, DOLBY, "dolby");
    assert(failed != NULL);
    free((char *)ctx.failed);
    ctx.failed = failed;
    ctx.calls = 0;
    assert(mc_candidates_select(requests, count, true, MockEnumerate, &ctx,
                                 &result, &relaxed) == 0);
    assert(relaxed && ctx.calls == 6 && strcmp(result.mime, HEVC) == 0);
    assert(strcmp(result.name, "underreported") == 0);
    free(result.name);
    free(failed);
}

static void TestEnumerationErrors(void)
{
    struct mock_enumeration ctx = {
        .components = mixed_components,
        .count = sizeof(mixed_components) / sizeof(mixed_components[0]),
    };
    struct mc_candidate_request requests[3];
    size_t count = mc_candidate_requests(requests, DOLBY, -1, HEVC, 2);
    struct mc_candidate result;
    bool relaxed;
    for (unsigned fail = 1; fail <= 3; fail++)
    {
        ctx.calls = 0;
        ctx.fail_call = fail;
        assert(mc_candidates_select(requests, count, true, MockEnumerate, &ctx,
                                     &result, &relaxed) == -77);
        assert(ctx.calls == fail && !relaxed && result.name == NULL);
    }
    ctx.count = 0;
    ctx.calls = 0;
    ctx.fail_call = 4;
    assert(mc_candidates_select(requests, count, true, MockEnumerate, &ctx,
                                 &result, &relaxed) == -77);
    assert(relaxed && ctx.calls == 4 && result.name == NULL);
    ctx.count = sizeof(mixed_components) / sizeof(mixed_components[0]);
    ctx.fail_call = 0;
    /* Fail at every name-copy/reallocation point, including after some
     * candidates were already accumulated in earlier MIME requests. */
    for (int budget = 0; budget < 8; budget++)
    {
        ctx.calls = 0;
        allocation_budget = budget;
        assert(mc_candidates_select(requests, count, true, MockEnumerate, &ctx,
                                     &result, &relaxed) == -2);
        assert(!relaxed && result.name == NULL);
    }
    allocation_budget = -1;
}

static void TestAllocationFailures(void)
{
    struct mc_candidates list = { 0 };
    assert(mc_candidates_add(&list, "keep", "video/hevc", 0, 1) == 0);
    for (int budget = 0; budget < 2; budget++)
    {
        allocation_budget = budget;
        assert(mc_candidates_add(&list, "new", "video/hevc", 1, 2) != 0);
        assert(list.count == 1 && strcmp(list.data[0].name, "keep") == 0);
    }
    allocation_budget = -1;
    mc_candidates_clear(&list);

    for (int budget = 0; budget < 3; budget++)
    {
        struct mc_score_rule *rules;
        unsigned invalid;
        allocation_budget = budget;
        assert(mc_score_rules_parse("a=1,b=2", &rules, &invalid) != 0);
        assert(rules == NULL);
    }
    allocation_budget = 0;
    assert(mc_candidate_fail(NULL, "video/hevc", "codec") == NULL);
    allocation_budget = -1;
}

int main(void)
{
    TestScores();
    TestOrdering();
    TestFailureSet();
    TestAllocationFailures();
    TestMixedSelection();
    TestProfileRelaxation();
    TestEnumerationErrors();
    return 0;
}
