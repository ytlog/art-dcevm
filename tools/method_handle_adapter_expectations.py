"""Fixed JBR capture/adapter results, including observable callback order."""

def expected_graphs(changed, value):
    icce = 'java.lang.IncompatibleClassChangeError'
    dispatch = icce if changed else str(value)
    graphs = {name:dispatch+':' for name in ('Inserted','AsTypeInside','ExplicitInside','NestedBindings',
        'CollectReceiver','FoldReceiver','ExplicitObject','ExplicitBoundObject','ExactInvoker','GenericInvoker')}
    graphs.update(InsertedNull='java.lang.NullPointerException:',UnusedBound='43:K',StaticBound='43:K',
        GuardFalse='43:TK',GuardNull='43:TK',FilterReplacement='7:P',FilterThrows='java.lang.IllegalStateException:X',
        FilterBadReference='java.lang.ClassCastException:',PermutedBadReference='java.lang.ClassCastException:',
        SpreadBadReference='java.lang.ClassCastException:',FilterNullPrimitive='java.lang.NullPointerException:',
        SpreadWrongLength='java.lang.IllegalArgumentException:',CollectorArity='java.lang.invoke.WrongMethodTypeException:',
        CatchFilter='39:XH')
    graphs.update({name:icce+':' if changed else str(value+1)+':R' for name in ('ReturnOutside','ReturnInside','InsertedGraph')})
    graphs.update(FilterInside=icce+':F' if changed else '10:F',FilterReceiver=dispatch+':S',GuardTrue=dispatch+':T',
        CatchInside='31:H' if changed else str(value)+':',CatchOutside='43:K' if changed else str(value+1)+':R',
        FoldInside=dispatch+':V',CollectInside=dispatch+':V',
        Permuted=(icce if changed else '8')+':',SpreadInside=(icce if changed else '8')+':',
        CollectorInside=(icce if changed else '2')+':',VarargsInside=(icce if changed else '2')+':',
        VarargsEmpty=(icce if changed else '0')+':',FixedNullArray=(icce if changed else 'java.lang.NullPointerException')+':')
    graphs.update({name:('java.lang.ClassCastException' if changed else str(value))+':'
        for name in ('AsTypeObject','AsTypeBoundObject','SpreadInvoker')})
    return graphs
