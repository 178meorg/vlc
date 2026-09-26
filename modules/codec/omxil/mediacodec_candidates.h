/* Copyright (C) 2026 VLC authors and VideoLAN
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef VLC_MEDIACODEC_CANDIDATES_H
#define VLC_MEDIACODEC_CANDIDATES_H

#include <errno.h>
#include <limits.h>
#include <regex.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct mc_candidate
{
    char *name;
    const char *mime; /* Borrowed from the request, normally a string literal. */
    int64_t score;
    int quirks;
};

struct mc_candidate_request
{
    const char *mime;
    int profile;
    int bonus;
    const char *required_mime;
};

static inline size_t mc_candidate_requests(struct mc_candidate_request requests[3],
                                          const char *mime, int profile,
                                          const char *base_mime, int base_profile)
{
    if (base_mime == NULL)
    {
        requests[0] = (struct mc_candidate_request) { mime, profile, 0, NULL };
        return 1;
    }
    requests[0] = (struct mc_candidate_request) { mime, profile, 500, base_mime };
    requests[1] = (struct mc_candidate_request) { mime, profile, 400, NULL };
    requests[2] = (struct mc_candidate_request) { base_mime, base_profile, 0, NULL };
    return 3;
}

struct mc_candidates
{
    struct mc_candidate *data;
    size_t count;
};

struct mc_score_rule
{
    regex_t regex;
    int score;
    struct mc_score_rule *next;
};

static inline void mc_score_rules_clear(struct mc_score_rule *rule)
{
    while (rule != NULL)
    {
        struct mc_score_rule *next = rule->next;
        regfree(&rule->regex);
        free(rule);
        rule = next;
    }
}

/* Invalid entries are ignored. Allocation failures abort the entire parse. */
static inline int mc_score_rules_parse(const char *text,
                                      struct mc_score_rule **rules,
                                      unsigned *invalid)
{
    *rules = NULL;
    *invalid = 0;
    if (text == NULL || *text == '\0')
        return 0;
    char *copy = strdup(text);
    if (copy == NULL)
        return -1;
    struct mc_score_rule **tail = rules;
    char *entry = copy;
    while (entry != NULL)
    {
        char *next = strchr(entry, ',');
        if (next != NULL)
            *next++ = '\0';
        while (*entry == ' ' || *entry == '\t')
            entry++;
        char *eq = strrchr(entry, '=');
        char *end;
        long score = 0;
        bool valid = eq != NULL && eq != entry;
        if (valid)
        {
            *eq = '\0';
            char *trim = eq;
            while (trim > entry && (trim[-1] == ' ' || trim[-1] == '\t'))
                *--trim = '\0';
            errno = 0;
            score = strtol(eq + 1, &end, 10);
            valid = end != eq + 1 && errno == 0 && score >= INT_MIN
                 && score <= INT_MAX && *entry != '\0';
            while (*end == ' ' || *end == '\t')
                end++;
            valid = valid && *end == '\0';
        }
        if (valid)
        {
            struct mc_score_rule *rule = malloc(sizeof(*rule));
            if (rule == NULL)
                goto error;
            int ret = regcomp(&rule->regex, entry, REG_EXTENDED | REG_NOSUB);
            if (ret == 0)
            {
                rule->score = score;
                rule->next = NULL;
                *tail = rule;
                tail = &rule->next;
            }
            else
            {
                free(rule);
                if (ret == REG_ESPACE)
                    goto error;
                valid = false;
            }
        }
        if (!valid)
            (*invalid)++;
        entry = next;
    }
    free(copy);
    return 0;
error:
    free(copy);
    mc_score_rules_clear(*rules);
    *rules = NULL;
    return -1;
}

static inline int mc_candidate_score(const struct mc_score_rule *rule,
                                     const char *name)
{
    for (; rule != NULL; rule = rule->next)
        if (regexec(&rule->regex, name, 0, NULL, 0) == 0)
            return rule->score;
    return 100;
}

static inline void mc_candidates_clear(struct mc_candidates *list)
{
    for (size_t i = 0; i < list->count; i++)
        free(list->data[i].name);
    free(list->data);
    *list = (struct mc_candidates) { 0 };
}

/* Insert stably, highest score first. A component can have several MIME modes. */
static inline int mc_candidates_add(struct mc_candidates *list,
                                    const char *name, const char *mime,
                                    int64_t score, int quirks)
{
    for (size_t i = 0; i < list->count; i++)
        if (strcmp(list->data[i].name, name) == 0 &&
            strcmp(list->data[i].mime, mime) == 0)
        {
            if (list->data[i].score >= score)
                return 0;
            struct mc_candidate entry = list->data[i];
            entry.score = score;
            entry.quirks = quirks;
            while (i > 0 && list->data[i - 1].score < score)
            {
                list->data[i] = list->data[i - 1];
                i--;
            }
            list->data[i] = entry;
            return 0;
        }
    char *copy = strdup(name);
    if (copy == NULL)
        return -1;
    if (list->count == (size_t)-1 / sizeof(*list->data))
    {
        free(copy);
        return -1;
    }
    struct mc_candidate *data = realloc(list->data,
                                       (list->count + 1) * sizeof(*data));
    if (data == NULL)
    {
        free(copy);
        return -1;
    }
    list->data = data;
    size_t pos = list->count++;
    while (pos > 0 && data[pos - 1].score < score)
    {
        data[pos] = data[pos - 1];
        pos--;
    }
    data[pos] = (struct mc_candidate) { copy, mime, score, quirks };
    return 0;
}

/* Decoder-private state: MIME<TAB>name<NEWLINE>. Never inherited globally. */
static inline bool mc_candidate_failed(const char *failed, const char *mime,
                                       const char *name)
{
    if (failed == NULL)
        return false;
    size_t m = strlen(mime), n = strlen(name);
    for (const char *entry = failed; *entry; )
    {
        const char *end = strchr(entry, '\n');
        if (end == NULL)
            break;
        if ((size_t)(end - entry) == m + n + 1 &&
            memcmp(entry, mime, m) == 0 && entry[m] == '\t' &&
            memcmp(entry + m + 1, name, n) == 0)
            return true;
        entry = end + 1;
    }
    return false;
}

static inline char *mc_candidate_fail(const char *failed, const char *mime,
                                      const char *name)
{
    if (failed == NULL)
        failed = "";
    if (mc_candidate_failed(failed, mime, name))
        return strdup(failed);
    size_t f = strlen(failed), m = strlen(mime), n = strlen(name);
    if (n > (size_t)-1 - 3 || m > (size_t)-1 - n - 3 ||
        f > (size_t)-1 - m - n - 3)
        return NULL;
    char *result = malloc(f + m + n + 3);
    if (result != NULL)
    {
        memcpy(result, failed, f);
        memcpy(result + f, mime, m);
        result[f + m] = '\t';
        memcpy(result + f + m + 1, name, n);
        result[f + m + n + 1] = '\n';
        result[f + m + n + 2] = '\0';
    }
    return result;
}

/* Apply the same exclusions during strict and relaxed enumeration. */
static inline int mc_candidates_offer(struct mc_candidates *list,
                                      const struct mc_candidate_request *request,
                                      const struct mc_score_rule *rules,
                                      const char *failed, const char *name,
                                      bool blacklisted, int quirks)
{
    if (blacklisted || mc_candidate_failed(failed, request->mime, name))
        return 0;
    int score = mc_candidate_score(rules, name);
    if (score < 0)
        return 0;
    return mc_candidates_add(list, name, request->mime,
                             (int64_t)score + request->bonus, quirks);
}

/* Enumerators append candidates and return zero on success, a negative error
 * otherwise. No partial result escapes on error. Returns 1 for an empty pool.
 * MIME strings must outlive the selected candidate; only its name is owned. */
typedef int (*mc_candidate_enumerate)(void *, const struct mc_candidate_request *,
                                    struct mc_candidates *);

static inline int mc_candidates_select(const struct mc_candidate_request *requests,
                                       size_t count, bool allow_relax,
                                       mc_candidate_enumerate enumerate, void *opaque,
                                       struct mc_candidate *result, bool *relaxed)
{
    struct mc_candidates list = { 0 };
    *result = (struct mc_candidate) { 0 };
    *relaxed = false;
    bool positive_profile = false;
    for (size_t i = 0; i < count; i++)
        positive_profile |= requests[i].profile > 0;
    for (unsigned pass = 0; pass < 2; pass++)
    {
        for (size_t i = 0; i < count; i++)
        {
            struct mc_candidate_request request = requests[i];
            if (pass && request.profile > 0)
                request.profile = -1;
            int ret = enumerate(opaque, &request, &list);
            if (ret != 0)
            {
                mc_candidates_clear(&list);
                return ret;
            }
        }
        if (list.count != 0)
        {
            *result = list.data[0];
            list.data[0].name = NULL;
            mc_candidates_clear(&list);
            return 0;
        }
        if (pass || !allow_relax || !positive_profile)
            break;
        *relaxed = true;
    }
    mc_candidates_clear(&list);
    return 1;
}

#endif
