#include "gh_releases.h"
#include <stdlib.h>
#include <string.h>

static const char *field(json_t *value, const char *key) {
    const char *s = json_string_value(json_object_get(value,key));
    return s ? s : "";
}
static bool add(GhDocument *doc, GhKind kind, const char *text, int level) {
    size_t n = strlen(text);
    if (doc->count == GH_DOC_NODES || n > GH_DOC_BYTES-doc->bytes) {
        doc->truncated = true; return false;
    }
    char *copy = strdup(text);
    if (!copy) { doc->truncated = true; return false; }
    doc->nodes[doc->count++] = (GhNode){.kind=kind,.text=copy,.level=level};
    doc->bytes += n; return true;
}
bool gh_document_releases(GhDocument *doc, json_t *releases, const char *repo,
                          const char *branch, bool dark) {
    gh_document_free(doc);
    doc->nodes = calloc(GH_DOC_NODES,sizeof *doc->nodes);
    if (!doc->nodes) return false;
    size_t i; json_t *rel;
    json_array_foreach(releases,i,rel) {
        if (i >= 12 || doc->truncated) break;
        if (json_is_true(json_object_get(rel,"draft"))) continue;
        const char *title = field(rel,"name");
        if (!add(doc,GH_RELEASE,*title?title:field(rel,"tag_name"),(int)i)) break;
        size_t ai; json_t *asset;
        json_array_foreach(json_object_get(rel,"assets"),ai,asset) {
            if (ai >= 6 || !add(doc,GH_ASSET,field(asset,"name"),(int)ai)) break;
        }
        GhDocument body = {0};
        if (!gh_document_parse(&body,field(rel,"body"),repo,branch,dark)) {
            doc->truncated = true; break;
        }
        unsigned group_base = (unsigned)doc->count + 1;
        for (int ni = 0; ni < body.count; ni++) {
            GhNode *node = &body.nodes[ni];
            size_t n = strlen(node->text) + (node->url?strlen(node->url):0);
            // Leave space for the card terminator even for an oversized body.
            if (doc->count >= GH_DOC_NODES-1 || n > GH_DOC_BYTES-doc->bytes) {
                doc->truncated = true; break;
            }
            node->group += group_base;
            doc->nodes[doc->count++] = *node; doc->bytes += n;
            memset(node,0,sizeof *node);
        }
        doc->truncated |= body.truncated;
        gh_document_free(&body);
        if (!add(doc,GH_RELEASE_END,"",(int)i)) break;
    }
    return true;
}
