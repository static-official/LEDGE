#!/usr/bin/env python3
"""Decode the captured official-client Authenticate auth-params segment."""
hx = ('e1071044463835323630384536434535393346d922d80752757365726e616d653d44463835323630384536434535393346'
      '26746f6b656e3d78316875367561716f6b34627a726a346e376f316463786f6f703567696871347778683939706d397179'
      '743169686d647762')
b = bytes.fromhex(hx)
i = 0
while i < len(b):
    key = b[i]; i += 1
    t = b[i]; i += 1
    print('key', key, 'type', hex(t))
    if t == 0x07:
        ln = b[i]; i += 1
        if ln & 0x80:
            ln = (ln & 0x7f) | (b[i] << 7); i += 1
        print('   STR len', ln, ':', b[i:i+ln].decode()); i += ln
    else:
        print('   non-STR type; following bytes:', b[i:i+10].hex())
        break
