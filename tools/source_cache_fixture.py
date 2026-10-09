"""Deterministic ordinary-Java cache oracle with >1024 IDs in all three tables.

ART dual caches allocate full arrays for small DEX ID tables. Genuine native
pair-format calibration therefore needs a large DEX, never fabricated entries.
"""
CACHE_PADDING=1025
MARKER='    // GENERATED_CACHE_PAIR_ID_PADDING'
def source_with_pair_ids(source):
    if source.count(MARKER)!=1:
        raise ValueError('Source cache fixture must contain one padding marker')
    fields=[];methods=[];types=[]
    for n in range(CACHE_PADDING):
        # Simple never-instantiated sibling types avoid expensive deeply nested
        # Java array type checking while supplying genuine distinct DEX type IDs.
        name=f'CachePairType{n:04d}'
        fields.append(f'    private static {name} cachePadding{n:04d};')
        methods.append(f'    private static int cachePaddingMethod{n:04d}() {{ return {n}; }}')
        types.append(f'final class {name} {{}}')
    return source.replace(MARKER,'\n'.join(fields+methods))+'\n'+'\n'.join(types)+'\n'

def validate_pair_id_counts(dex):
    import struct
    if len(dex)<112 or not dex.startswith(b'dex\n'):
        raise ValueError('Source cache oracle requires complete StandardDEX header')
    counts=tuple(struct.unpack_from('<I',dex,offset)[0] for offset in (64,80,88))
    if any(count<=1024 for count in counts):
        raise ValueError(f'Source cache oracle cannot prove pair format with small type/field/method IDs: {counts}')
    return counts
