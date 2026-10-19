#!/system/bin/sh
# Debug probe only. Disable startup promotion; JVMTI may still promote IDs later.
cmd=$1
shift
exec "$cmd" -Xopaque-jni-ids:swapable -Xauto-promote-opaque-jni-ids:false \
    -Xcompiler-option --debuggable \
    -XjdwpProvider:adbconnection -XjdwpOptions:suspend=n,server=y "$@"
