#include "../../source/gh_markup.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static GhDocument d;
static void parse(const char *s) { assert(gh_document_parse(&d,s,"owner/repo","main",true)); }
static int kinds(GhKind k) {int n=0;for(int i=0;i<d.count;i++)n+=d.nodes[i].kind==k;return n;}
static int has(const char *s){for(int i=0;i<d.count;i++)if(strstr(d.nodes[i].text,s))return 1;return 0;}
int main(void) {
 parse("# Heading\ntext &amp; **bold**\n```c\n  if (x < 2) return \"<xml>\";\n```\n> quote\n---");
 assert(kinds(GH_HEADING)==1 && kinds(GH_CODE)==1 && kinds(GH_QUOTE)==1 && kinds(GH_RULE)==1);
 assert(has("text & ")&&has("bold")&&has("  if (x < 2) return \"<xml>\";"));
 int bold=0;for(int i=0;i<d.count;i++)if(!strcmp(d.nodes[i].text,"bold"))bold=d.nodes[i].style&2;assert(bold);
 parse("A paragraph split\nover source lines with [a link](README.md)\nand **bold** text.\n\nNext paragraph.\n- one\n- two\n");
 assert(has("A paragraph split over source lines with "));assert(has("Next paragraph."));
 unsigned first=d.nodes[0].group;for(int i=0;i<d.count;i++)if(strstr(d.nodes[i].text,"bold"))assert(d.nodes[i].group==first);
 parse("hard break  \nnext line\n\n<p align=\"center\">links<br><sub>small footer</sub></p>");
 assert(!has("hard break  next line"));int sub=0;for(int i=0;i<d.count;i++)if(strstr(d.nodes[i].text,"small footer"))sub=d.nodes[i].style&8;assert(sub);
 parse("<p align=\"center\"><b>Readable</b> text <a href=\"#features\">Features</a></p><h2>Features</h2>");
 assert(kinds(GH_LINK)==1&&!strcmp(d.nodes[2].url,"#features"));assert((d.nodes[0].style&3)==3);
 parse("<p align=\"center\"><a href=\"https://example.com\"><img src=\"badge.svg\"></a><img src=\"second.svg\"></p>");
 assert(kinds(GH_IMAGE)==2&&kinds(GH_LINK)==1&&d.nodes[0].group==d.nodes[2].group&&!*d.nodes[1].text);
 parse("before ![a](a.png) between ![b](b.png) after [one](a.md) [two](https://example.com)");
 assert(kinds(GH_IMAGE)==2&&kinds(GH_LINK)==2&&has("between")&&has("after"));
 assert(strstr(d.nodes[1].url,"raw.githubusercontent.com/owner/repo/main/a.png"));
 parse("[![Badge](badge.svg)](https://example.com)");assert(kinds(GH_IMAGE)==1&&kinds(GH_LINK)==1);
 parse("<picture><source media=\"(prefers-color-scheme: dark)\" srcset=\"dark.svg\"><img src=\"light.svg\" alt=\"card\"></picture><img src=\"other.png\">");
 assert(kinds(GH_IMAGE)==2&&strstr(d.nodes[0].url,"dark.svg")&&strstr(d.nodes[1].url,"other.png"));
 parse("<script>bad()</script><style>evil</style><!-- hidden --><h2>Safe</h2><a href=\"https://example.com\">Link</a>");
 assert(!has("bad")&&!has("evil")&&!has("hidden")&&has("Safe")&&kinds(GH_LINK)==1);
 parse("[bad](javascript:alert) ![bad](file:///etc/passwd) [relative](./README.md)"); assert(kinds(GH_LINK)==1&&kinds(GH_IMAGE)==0);
 parse("<table><tr><td><strong>First</strong><br>Body [link](README.md)</td><td>Second</td></tr><tr><td>third</td><td>fourth</td></tr></table>");
 assert(d.nodes[0].table&&d.nodes[0].table_cols==2&&d.nodes[0].table_row==0&&d.nodes[0].table_col==0);
 assert(d.nodes[d.count-1].table_row==1&&d.nodes[d.count-1].table_col==1);assert(kinds(GH_LINK)==1);
 parse("| Guide | Purpose |\n| --- | --- |\n| [Controls](README.md) | [Download](https://example.com) |\n\nAfter");
 assert(d.nodes[0].table_header&&d.nodes[0].table_cols==2);assert(kinds(GH_LINK)==2);assert(!d.nodes[d.count-1].table);
 parse("| lone line\nnormal paragraph");assert(!d.nodes[0].table);
 parse("- **Top:** first line\n  continued here.\n- second\n  - nested\n\nAfter");
 assert(d.nodes[0].style&32);assert(d.nodes[0].style&2);assert(has("continued here."));
 int nested=0;for(int i=0;i<d.count;i++)nested|=d.nodes[i].list_depth>0;assert(nested);
 parse("Inline `code_value` and **bold**.\n```\n**literal_stars**\n```");
 int mono=0,literal=0;for(int i=0;i<d.count;i++){mono|=d.nodes[i].style&64;literal|=d.nodes[i].kind==GH_CODE&&!strcmp(d.nodes[i].text,"**literal_stars**");}assert(mono&&literal);
 char *large=malloc(GH_DOC_BYTES+100);assert(large);memset(large,'x',GH_DOC_BYTES+99);large[GH_DOC_BYTES+99]=0;parse(large);assert(d.truncated);free(large);
 char fuzz[2049];unsigned r=123456;
 const char alphabet[]="|:[]()<>/'\"`#*_-! abc\n\t&;0123456789";
 for(int t=0;t<4000;t++){unsigned n=(r%2048);for(unsigned j=0;j<n;j++){r=r*1664525+1013904223;fuzz[j]=alphabet[r%(sizeof alphabet-1)];}fuzz[n]=0;parse(fuzz);assert(d.count<=GH_DOC_NODES&&d.bytes<=GH_DOC_BYTES);}
 gh_document_free(&d);gh_document_free(&d);puts("PASS: README markup, links/images/themes, unsafe schemes, bounds, 4000 mutations");
}
