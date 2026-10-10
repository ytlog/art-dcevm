"""Independent build-time D8 invocation form validation."""
import re
def validate_receiver_dump(dump):
    for kind in ('Virtual','Super','Direct','Static','Interface'):
        for range_ in (False,True):
            name='call'+kind+('Range' if range_ else 'Small')
            marker="name          : '"+name+"'"
            if dump.count(marker)!=1: raise ValueError('Missing or duplicate receiver caller '+name)
            body=dump.split(marker,1)[1].split('    #',1)[0]
            opcode='invoke-'+kind.lower()+('/range' if range_ else '')
            instructions=re.findall(r'\|[0-9a-f]+: (invoke-[^ ]+)',body)
            if instructions!=[opcode]: raise ValueError('Unexpected actual receiver invocation '+name+': '+repr(instructions))
