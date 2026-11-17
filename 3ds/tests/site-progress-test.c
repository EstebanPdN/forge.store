// Host test for site_progress.h (the same header the console build uses).
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "site_progress.h"
static int value_of(SiteProgress *p, int n, const char *card) {
    for (int i = 0; i < n; i++) if (!strcmp(p[i].card, card)) return p[i].value;
    return -1;
}
int main(int argc, char **argv) {
    SiteProgress p[SITE_PROGRESS_MAX];
    const char *html =
        "<article class=\"progress-card port-worms\"><strong>70<small>%</small></strong>"
        "<div role=\"progressbar\" aria-valuemin=\"0\" aria-valuemax=\"100\" aria-valuenow=\"70\"></div></article>"
        // A card without a value must not take the next card's value.
        "<article class=\"progress-card port-empty\"></article>"
        "<article class=\"progress-card port-viper extra\"><div aria-valuenow=\"50\"></div></article>"
        "<article class=\"progress-card port-bad\"><div aria-valuenow=\"150\"></div></article>"
        "<article class=\"progress-card port-neg\"><div aria-valuenow=\"x7\"></div></article>"
        "<article class=\"progress-card port-worms\"><div aria-valuenow=\"10\"></div></article>"
        "<article class=\"progress-card port-cut\"><div aria-valuenow=\"4";
    int n = site_progress_parse(html, strlen(html), p, SITE_PROGRESS_MAX);
    assert(n == 2);
    assert(value_of(p, n, "port-worms") == 70);   // first card wins
    assert(value_of(p, n, "port-viper") == 50);   // extra classes after the card name
    assert(value_of(p, n, "port-empty") == -1 && value_of(p, n, "port-bad") == -1 && value_of(p, n, "port-cut") == -1);
    assert(site_progress_parse(html, 0, p, SITE_PROGRESS_MAX) == 0);
    assert(site_progress_parse(html, strlen(html), p, 1) == 1);
    if (argc > 1) {  // a saved copy of the live page
        FILE *f = fopen(argv[1], "rb"); assert(f);
        fseek(f, 0, SEEK_END); long size = ftell(f); fseek(f, 0, SEEK_SET);
        char *page = malloc(size); assert(page && fread(page, 1, size, f) == (size_t)size); fclose(f);
        n = site_progress_parse(page, size, p, SITE_PROGRESS_MAX);
        assert(n >= 4);
        for (int i = 0; i < n; i++) printf("  %s = %d\n", p[i].card, p[i].value);
        free(page);
    }
    printf("PASS: site progress cards (bounds, per-card values, duplicates, truncation)\n");
}
