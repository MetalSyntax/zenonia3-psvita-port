#include <string.h>

#include "htmltext.h"

static int append_char(char *out, int *len, int cap, char c) {
    if (*len + 1 >= cap) return 0;
    out[(*len)++] = c;
    return 1;
}

// Busqueda de substring acotada (memmem no es portable/esta en newlib de
// vitasdk) -- usada solo para buscar "style3"/"style2b" dentro del texto de
// un tag <td ...> ya delimitado por su propio '>'.
static int memmem_style(const char *hay, int hay_len, const char *needle) {
    int needle_len = (int) strlen(needle);
    for (int i = 0; i + needle_len <= hay_len; i++) {
        if (!memcmp(hay + i, needle, needle_len)) return 1;
    }
    return 0;
}

int htmltext_extract(const char *html, char *out, int out_cap) {
    if (out_cap <= 0) return 0;

    const char *p = strstr(html, "<body>");
    p = p ? p + 6 : html;
    const char *end = strstr(p, "</body>");
    if (!end) end = p + strlen(p);

    int len = 0;
    int line_has_content = 0; // hubo al menos un caracter visible en la linea actual

    while (p < end && *p) {
        if (*p == '<') {
            if (!strncmp(p, "<style", 6)) {
                const char *close = strstr(p, "</style>");
                p = close ? close + 8 : end;
                continue;
            }
            if (!strncmp(p, "<script", 7)) {
                const char *close = strstr(p, "</script>");
                p = close ? close + 9 : end;
                continue;
            }
            // Tags que cierran una linea logica (fin de celda/fila/parrafo).
            if (!strncmp(p, "</td>", 5) || !strncmp(p, "</tr>", 5) ||
                !strncmp(p, "</p>", 4) || !strncmp(p, "<br", 3)) {
                if (line_has_content) {
                    append_char(out, &len, out_cap, '\n');
                    line_has_content = 0;
                }
            }
            // <td class="style3"/"style2b">: encabezado de seccion / etiqueta
            // (ver htmltext.h) -- marcar el INICIO de esta linea antes de
            // procesar su contenido. Solo tiene sentido al arrancar una linea
            // nueva (si ya hay contenido, este <td> es de OTRA fila y ya se
            // habria cerrado con </td>/</tr> arriba).
            if (!line_has_content && !strncmp(p, "<td", 3)) {
                const char *tag_end = p;
                while (*tag_end && *tag_end != '>') tag_end++;
                int tag_len = (int) (tag_end - p);
                if (tag_len > 6) {
                    if (memmem_style(p, tag_len, "style3")) {
                        append_char(out, &len, out_cap, HTMLTEXT_STYLE_HEADER);
                    } else if (memmem_style(p, tag_len, "style2b")) {
                        append_char(out, &len, out_cap, HTMLTEXT_STYLE_LABEL);
                    }
                }
            }
            // Saltear el tag hasta su '>' de cierre.
            p++;
            while (*p && *p != '>') p++;
            if (*p == '>') p++;
            continue;
        }
        if (*p == '&') {
            if (!strncmp(p, "&amp;", 5))  { append_char(out, &len, out_cap, '&');  p += 5; line_has_content = 1; continue; }
            if (!strncmp(p, "&lt;", 4))   { append_char(out, &len, out_cap, '<');  p += 4; line_has_content = 1; continue; }
            if (!strncmp(p, "&gt;", 4))   { append_char(out, &len, out_cap, '>');  p += 4; line_has_content = 1; continue; }
            if (!strncmp(p, "&quot;", 6)) { append_char(out, &len, out_cap, '"');  p += 6; line_has_content = 1; continue; }
            if (!strncmp(p, "&nbsp;", 6)) { append_char(out, &len, out_cap, ' ');  p += 6; continue; }
            if (!strncmp(p, "&#39;", 5))  { append_char(out, &len, out_cap, '\''); p += 5; line_has_content = 1; continue; }
            append_char(out, &len, out_cap, '&');
            p++;
            line_has_content = 1;
            continue;
        }

        unsigned char c = (unsigned char) *p;
        if (c >= 0x80) {
            // Sin decoder de la codepage real (euc-kr/etc.) no hay forma
            // confiable de mapear esto a un glifo -- se descarta.
            p++;
            continue;
        }
        if (c == '\r' || c == '\n' || c == '\t') c = ' ';
        if (c == ' ') {
            if (!line_has_content || out[len - 1] == ' ') { p++; continue; } // colapsar espacios, no arrancar linea con espacio
        } else {
            line_has_content = 1;
        }
        append_char(out, &len, out_cap, (char) c);
        p++;
    }
    if (line_has_content) append_char(out, &len, out_cap, '\n');

    out[len < out_cap ? len : out_cap - 1] = '\0';
    return len;
}
