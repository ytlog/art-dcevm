#include "platform/common/interpreter_notifications.h"
namespace artdcevm::interpreter_events {
namespace { NotificationLeases leases; }
jvmtiError BoundNotification(jvmtiEnv* env, bool value) { return leases.Bound(env, value); }
jvmtiError WatchNotification(jvmtiEnv* env, bool value) { return leases.Watch(env, value); }
jvmtiError PinNotifications(jvmtiEnv* env) { return leases.Pin(env); }
jvmtiError ReleaseNotifications(jvmtiEnv* env) { return leases.Release(env); }
void CommitNotifications() { leases.Committed(); }
}
