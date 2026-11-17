#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

// Reads the progress cards of my website (https://estebanpdn.github.io/, "Progress" page):
//   <article class="progress-card port-worms"> … aria-valuenow="70" … </article>
// Pure and bounded so the host tests run the same code as the console.
enum { SITE_PROGRESS_MAX = 32, SITE_PROGRESS_CARD = 48 };
typedef struct { char card[SITE_PROGRESS_CARD]; int value; } SiteProgress;

static inline const char *site_progress_find(const char *s, const char *end, const char *needle) {
    size_t n = strlen(needle);
    for (; s && (size_t)(end - s) >= n; s++)
        if (!memcmp(s, needle, n)) return s;
    return NULL;
}
/** Number of cards read into out (each card once, value 0..100). */
static inline int site_progress_parse(const char *html, size_t length, SiteProgress *out, int max) {
    static const char open[] = "class=\"progress-card ", value[] = "aria-valuenow=\"";
    const char *end = html + length, *at = html;
    int count = 0;
    while (count < max && (at = site_progress_find(at, end, open))) {
        at += sizeof open - 1;
        size_t n = 0;
        while (at + n < end && at[n] != '"' && at[n] != ' ' && n < SITE_PROGRESS_CARD - 1) n++;
        if (!n || at + n >= end || (at[n] != '"' && at[n] != ' ')) continue;
        // The value must belong to this card: stop at the next card.
        const char *next = site_progress_find(at, end, open), *limit = next ? next : end;
        const char *v = site_progress_find(at, limit, value);
        if (!v) continue;
        v += sizeof value - 1;
        int number = 0, digits = 0;
        while (v < limit && *v >= '0' && *v <= '9' && digits < 4) number = number * 10 + (*v++ - '0'), digits++;
        if (!digits || v >= limit || *v != '"' || number > 100) continue;
        bool seen = false;
        for (int i = 0; i < count; i++)
            if (strlen(out[i].card) == n && !memcmp(out[i].card, at, n)) seen = true;
        if (seen) continue;
        memcpy(out[count].card, at, n);
        out[count].card[n] = 0;
        out[count].value = number;
        count++;
    }
    return count;
}
