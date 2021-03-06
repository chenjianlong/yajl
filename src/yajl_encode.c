/*
 * Copyright (c) 2007-2011, Lloyd Hilaiel <lloyd@hilaiel.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "yajl_encode.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static void Utf16ToHex(unsigned short utf16, char * hexBuf)
{
    const char * hexchar = "0123456789ABCDEF";
    hexBuf[0] = hexchar[(utf16 >> 12) & 0x0F];
    hexBuf[1] = hexchar[(utf16 >> 8) & 0x0F];
    hexBuf[2] = hexchar[(utf16 >> 4)  & 0x0F];
    hexBuf[3] = hexchar[ utf16        & 0x0F];
}

void
yajl_string_encode(const yajl_print_t print,
                   void * ctx,
                   const unsigned char * str,
                   size_t len,
                   int escape_solidus)
{
    size_t beg = 0;
    size_t end = 0;
    char hexBuf[12];
    hexBuf[0] = '\\'; hexBuf[1] = 'u';
    hexBuf[6] = '\\'; hexBuf[7] = 'u';

    while (end < len) {
        const char * escaped = NULL;
        unsigned int escapedLen = 6;
        unsigned char c = (unsigned char) str[end];
        size_t mark = end;
        unsigned short utf16 = 0;
        switch (c) {
            case '\r': escaped = "\\r"; escapedLen = 2; break;
            case '\n': escaped = "\\n"; escapedLen = 2; break;
            case '\\': escaped = "\\\\"; escapedLen = 2; break;
            /* it is not required to escape a solidus in JSON:
             * read sec. 2.5: http://www.ietf.org/rfc/rfc4627.txt
             * specifically, this production from the grammar:
             *   unescaped = %x20-21 / %x23-5B / %x5D-10FFFF
             */
            case '/': if (escape_solidus) { escaped = "\\/"; escapedLen = 2; } break;
            case '"': escaped = "\\\""; escapedLen = 2; break;
            case '\f': escaped = "\\f"; escapedLen = 2; break;
            case '\b': escaped = "\\b"; escapedLen = 2; break;
            case '\t': escaped = "\\t"; escapedLen = 2; break;
            default:
                switch (c & 0xF8) { /* 11111 000 */
                    case 0: break;
                    case 0xC0: /* 110 00 000 */
                    case 0xC8: /* 110 01 000 */
                    case 0xD0: /* 110 10 000 */
                    case 0xD8: /* 110 11 000 */
                        escaped = hexBuf;
                        utf16 = (c & 0x1F) << 6;
                        if ((c = str[++end]) & 0x80) {
                            utf16 |= c & 0x3F;
                        } else {
                            utf16 = str[--end];
                        }
                        Utf16ToHex (utf16, hexBuf + 2);
                        break;
                    case 0xE0: /* 1110 0 000 */
                    case 0xE8: /* 1110 1 000 */
                        escaped = hexBuf;
                        utf16 = (c & 0x0F) << 12;
                        if ((c = str[++end]) & 0x80) {
                            utf16 |= (c & 0x3F) << 6;

                            if ((c = str[++end]) & 0x80) {
                                utf16 |= c & 0x3F;
                                Utf16ToHex (utf16, hexBuf + 2);
                                break;
                            }
                        }

                        Utf16ToHex (str[mark], hexBuf + 2);
                        end = mark;
                        break;
                    case 0xF0: /* 11110 000 */
                        escaped = hexBuf;
                        utf16 = (0xD8 << 8) | ((c & 0x07) << 8);
                        if ((c = str[++end]) & 0x80) {
                            utf16 |= c << 2;

                            if ((c = str[++end]) & 0x80) {
                                utf16 |= (c & 0x30) >> 4;
                                utf16 -= 1 << 6;
                                Utf16ToHex (utf16, hexBuf + 2);

                                utf16 |= (0xD9 << 8) | (c & 0x0F) << 6;

                                if ((c = str[++end]) & 0x80) {
                                    utf16 |= c & 0x3F;
                                    Utf16ToHex (utf16, hexBuf + 8);
                                    escapedLen = 12;
                                    break;
                                }
                            }
                        }

                        Utf16ToHex (str[mark], hexBuf + 2);
                        end = mark;
                        break;
                }
                break;
        }
        if (escaped != NULL) {
            print(ctx, (const char *) (str + beg), mark - beg);
            print(ctx, escaped, escapedLen);
            beg = ++end;
        } else {
            ++end;
        }
    }
    print(ctx, (const char *) (str + beg), end - beg);
}

static void hexToDigit(unsigned int * val, const unsigned char * hex)
{
    unsigned int i;
    for (i=0;i<4;i++) {
        unsigned char c = hex[i];
        if (c >= 'A') c = (c & ~0x20) - 7;
        c -= '0';
        assert(!(c & 0xF0));
        *val = (*val << 4) | c;
    }
}

static void Utf32toUtf8(unsigned int codepoint, char * utf8Buf)
{
    if (codepoint < 0x80) {
        utf8Buf[0] = (char) codepoint;
        utf8Buf[1] = 0;
    } else if (codepoint < 0x0800) {
        utf8Buf[0] = (char) ((codepoint >> 6) | 0xC0);
        utf8Buf[1] = (char) ((codepoint & 0x3F) | 0x80);
        utf8Buf[2] = 0;
    } else if (codepoint < 0x10000) {
        utf8Buf[0] = (char) ((codepoint >> 12) | 0xE0);
        utf8Buf[1] = (char) (((codepoint >> 6) & 0x3F) | 0x80);
        utf8Buf[2] = (char) ((codepoint & 0x3F) | 0x80);
        utf8Buf[3] = 0;
    } else if (codepoint < 0x200000) {
        utf8Buf[0] =(char)((codepoint >> 18) | 0xF0);
        utf8Buf[1] =(char)(((codepoint >> 12) & 0x3F) | 0x80);
        utf8Buf[2] =(char)(((codepoint >> 6) & 0x3F) | 0x80);
        utf8Buf[3] =(char)((codepoint & 0x3F) | 0x80);
        utf8Buf[4] = 0;
    } else {
        utf8Buf[0] = '?';
        utf8Buf[1] = 0;
    }
}

void yajl_string_decode(yajl_buf buf, const unsigned char * str,
                        size_t len)
{
    size_t beg = 0;
    size_t end = 0;

    while (end < len) {
        if (str[end] == '\\') {
            char utf8Buf[5];
            const char * unescaped = "?";
            yajl_buf_append(buf, str + beg, end - beg);
            switch (str[++end]) {
                case 'r': unescaped = "\r"; break;
                case 'n': unescaped = "\n"; break;
                case '\\': unescaped = "\\"; break;
                case '/': unescaped = "/"; break;
                case '"': unescaped = "\""; break;
                case 'f': unescaped = "\f"; break;
                case 'b': unescaped = "\b"; break;
                case 't': unescaped = "\t"; break;
                case 'u': {
                    unsigned int codepoint = 0;
                    hexToDigit(&codepoint, str + ++end);
                    end+=3;
                    /* check if this is a surrogate */
                    if ((codepoint & 0xFC00) == 0xD800) {
                        end++;
                        if (str[end] == '\\' && str[end + 1] == 'u') {
                            unsigned int surrogate = 0;
                            hexToDigit(&surrogate, str + end + 2);
                            codepoint =
                                (((codepoint & 0x3F) << 10) |
                                 ((((codepoint >> 6) & 0xF) + 1) << 16) |
                                 (surrogate & 0x3FF));
                            end += 5;
                        } else {
                            unescaped = "?";
                            break;
                        }
                    }

                    Utf32toUtf8(codepoint, utf8Buf);
                    unescaped = utf8Buf;

                    if (codepoint == 0) {
                        yajl_buf_append(buf, unescaped, 1);
                        beg = ++end;
                        continue;
                    }

                    break;
                }
                default:
                    assert("this should never happen" == NULL);
            }
            yajl_buf_append(buf, unescaped, (unsigned int)strlen(unescaped));
            beg = ++end;
        } else {
            end++;
        }
    }
    yajl_buf_append(buf, str + beg, end - beg);
}

#define ADV_PTR s++; if (!(len--)) return 0;

int yajl_string_validate_utf8(const unsigned char * s, size_t len)
{
    if (!len) return 1;
    if (!s) return 0;

    while (len--) {
        /* single byte */
        if (*s <= 0x7f) {
            /* noop */
        }
        /* two byte */
        else if ((*s >> 5) == 0x6) {
            ADV_PTR;
            if (!((*s >> 6) == 0x2)) return 0;
        }
        /* three byte */
        else if ((*s >> 4) == 0x0e) {
            ADV_PTR;
            if (!((*s >> 6) == 0x2)) return 0;
            ADV_PTR;
            if (!((*s >> 6) == 0x2)) return 0;
        }
        /* four byte */
        else if ((*s >> 3) == 0x1e) {
            ADV_PTR;
            if (!((*s >> 6) == 0x2)) return 0;
            ADV_PTR;
            if (!((*s >> 6) == 0x2)) return 0;
            ADV_PTR;
            if (!((*s >> 6) == 0x2)) return 0;
        } else {
            return 0;
        }

        s++;
    }

    return 1;
}
