#!/usr/bin/env python3
"""Run the ordered structural-redefinition checks on a verified debug APK."""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

from apk_verification import base_apk_path, require_install_success, verified_apk_sha256
from evidence_paths import new_evidence_directory


ROOT = Path(__file__).resolve().parents[1]
PACKAGE = "dev.artdcevm.lab"
COMPONENT = PACKAGE + "/com.github.ytlog.artdcevm.testing.StructuralRedefineInstrumentation"
REPORT_PATH = "files/structural-redefine.json"
SCENARIOS = (
    ("body", ["body"], 0),
    ("add", ["add"], 0),
    ("method-delete", ["method-delete"], 0),
    ("method-restore", ["add"], 0),
    ("hierarchy", ["hierarchy"], 0),
    ("atomic-modifier-public-class-interface", ["modifier-public", "class-interface"], 70),
    ("modifier-public", ["modifier-public"], 0),
    ("modifier-final", ["modifier-final"], 0),
    ("modifier-sync", ["modifier-sync"], 0),
    ("modifier-field-final", ["modifier-field-final"], 0),
    ("class-final", ["class-final"], 0),
    ("class-nonfinal", ["class-nonfinal"], 0),
    ("class-abstract-instances", ["class-abstract"], 70),
    ("class-abstract", ["candidate-abstract"], 0),
    ("class-final-subclassed", ["modifier-class-final"], 70),
    ("atomic-method-retire-class-interface", ["methods-delete", "class-interface"], 70),
    ("native-change", ["native-change"], 0),
    ("constructor-delete", ["constructor-delete"], 0),
    ("abstract-method-delete", ["methods-abstract-delete"], 0),
    ("abstract-method-make-abstract", ["methods-abstract-concrete"], 0),
    ("abstract-method-restore", ["methods-abstract-original"], 0),
    ("clinit-delete", ["clinit-delete"], 0),
    ("methods-retire", ["methods-delete"], 0),
    ("methods-restore", ["methods-original"], 0),
    ("methods-retire-again", ["methods-delete"], 0),
    ("methods-restore-again", ["methods-original"], 0),
    ("method-private", ["method-private"], 71),
    ("method-storage", ["method-storage"], 71),
    ("method-storage-delete", ["method-storage-delete"], 0),
    ("method-storage-readd", ["method-storage"], 71),
    ("method-storage-restore", ["method-storage-original"], 0),
    ("interface-method-bodies", ["interface-method-bodies"], 0),
    ("nested-interface-body", ["nested-interface-body"], 0),
    ("nested-interface-add", ["nested-interface-add"], 63),
    ("nested-interface-modifiers", ["nested-interface-modifiers"], 63),
    ("nested-interface-parents", ["nested-interface-parents"], 63),
    ("nested-interface-signature", ["nested-interface-signature"], 0),
    ("nested-interface-restore-signature", ["nested-interface-body"], 0),
    ("nested-interface-delete", ["nested-interface-delete"], 0),
    ("nested-interface-restore-delete", ["nested-interface-body"], 0),
    ("nested-interface-field", ["nested-interface-field"], 0),
    ("nested-interface-restore-field", ["nested-interface-body"], 0),
    ("nested-interface-initializer", ["nested-interface-initializer"], 0),
    ("nested-interface-restore-initializer", ["nested-interface-body"], 0),
    ("interface-after-nested", ["interface-default-body"], 0),
    ("interface-kind-default", ["interface-kind-default", "interface-kind-default"], 0),
    ("interface-kind-abstract-missing", ["interface-kind-abstract-missing"], 63),
    ("interface-kind-abstract", ["interface-kind-abstract", "interface-kind-abstract"], 0),
    ("interface-parent-missing", ["interface-parent-missing"], 63),
    ("interface-parent-add", ["interface-parent-add", "interface-parent-add"], 0),
    ("interface-parent-remove", ["interface-parent-original"], 66),
    ("interface-parent-batch", ["interface-parent-batch", "interface-parent-batch"], 0),
    ("interface-parent-rebuilt", ["interface-parent-rebuilt", "interface-parent-rebuilt"], 0),
    ("interface-access", ["interface-access"], 0),
    ("interface-access-restore", ["interface-access-restore"], 0),
    ("interface-kind-static-rejected", ["interface-kind-static"], 71),
    ("interface-kind-static", ["interface-kind-static", "interface-kind-static"], 0),
    ("interface-kind-static-restore", ["interface-kind-static-restore", "interface-kind-static-restore"], 0),
    ("interface-batch-unrelated", ["interface-batch-body", "interface-batch-body"], 0),
    ("interface-batch-unrelated-invalid", ["interface-batch-invalid", "interface-batch-next"], 71),
    ("interface-batch-after-rejection", ["interface-batch-next"], 0),
    ("interface-batch-unrelated-reverse", ["interface-batch-body", "interface-batch-body"], 0),
    ("interface-implementation-body", ["interface-implementation-body", "interface-implementation-body"], 0),
    ("interface-implementation-add", ["interface-implementation-add", "interface-implementation-add"], 0),
    ("interface-implementation-child", ["interface-implementation-child", "interface-implementation-child", "interface-implementation-child"], 0),
    ("interface-implementation-child-only", ["interface-implementation-child-only", "interface-implementation-child-only"], 0),
    ("interface-implementation-retire", ["interface-implementation-retire", "interface-implementation-retire"], 0),
    ("interface-implementation-invalid", ["interface-implementation-invalid", "interface-implementation-invalid"], 71),
    ("interface-implementation-abstract", ["interface-implementation-abstract", "interface-implementation-abstract"], 70),
    ("interface-implementation-recovery", ["interface-implementation-next"], 0),
    ("interface-overlap-body", ["interface-overlap-body", "interface-overlap-body"], 0),
    ("interface-overlap-structural", ["interface-overlap-structural", "interface-overlap-structural"], 0),
    ("interface-overlap-recovery", ["interface-overlap-next"], 0),
    ("interface-overlap-superclass", ["interface-overlap-superclass", "interface-overlap-superclass"], 66),
    ("interface-overlap-uncovered", ["interface-overlap-uncovered", "interface-overlap-uncovered"], 0),
    ("multi-interface-unrelated", ["multi-interface-body", "multi-interface-body"], 0),
    ("multi-interface-invalid", ["multi-interface-next", "multi-interface-invalid"], 63),
    ("multi-interface-recovery", ["multi-interface-next", "multi-interface-next"], 0),
    ("multi-interface-family", ["multi-interface-body", "multi-interface-body"], 0),
    ("multi-interface-shared", ["multi-interface-body", "multi-interface-body"], 0),
    ("multi-interface-after-shared", ["multi-interface-next"], 0),
    ("multi-interface-covered", ["multi-interface-covered", "multi-interface-covered"], 0),
    ("interface-pointer-target", ["interface-pointer-body"], 79),
    ("interface-pointer-subtype", ["interface-pointer-body"], 79),
    ("interface-pointer-recovery", ["interface-pointer-body"], 0),
    ("interface-default-body", ["interface-default-body"], 0),
    ("copied-default-add", ["copied-default-add"], 0),
    ("interface-stress", ["interface-stress"], 0),
    ('interface-members-add', ['interface-members-add'], 0),
    ('interface-members-signature', ['interface-members-signature'], 0),
    ('interface-members-delete', ['interface-members-delete'], 0),
    ('interface-default-add', ['interface-default-add'], 0),
    ('interface-miranda-prepare', ['interface-miranda-prepare'], 0),
    ('interface-miranda-add', ['interface-miranda-add'], 0),
    ('interface-default-conflict', ['interface-default-conflict'], 63),
    ('interface-hide-parent-static', ['interface-hide-parent-static'], 63),
    ('interface-hide-parent-private', ['interface-hide-parent-private'], 63),
    ('interface-hide-object', ['interface-hide-object'], 63),
    ('interface-shadow-static', ['interface-shadow-static'], 63),
    ('interface-shadow-private', ['interface-shadow-private'], 63),
    ('interface-shadow-protected', ['interface-shadow-protected'], 63),
    ('interface-shadow-package', ['interface-shadow-package'], 63),
    ('interface-bridge-add', ['interface-bridge-add', 'interface-bridge-add'], 0),
    ('interface-lambda-first', ['interface-lambda-first'], 0),
    ('interface-lambda-append', ['interface-lambda-append', 'interface-lambda-append'], 0),
    ('interface-lambda-insert', ['interface-lambda-insert', 'interface-lambda-insert', 'interface-lambda-insert'], 0),
    ('interface-abstract-batch-forward', ['interface-abstract-add', 'interface-abstract-add', 'interface-abstract-add'], 0),
    ('interface-abstract-batch-reverse', ['interface-abstract-add', 'interface-abstract-add', 'interface-abstract-add'], 0),
    ('interface-abstract-existing', ['interface-abstract-add'], 0),
    ('interface-abstract-inherited', ['interface-abstract-add'], 0),
    ('interface-abstract-defaultimplementation', ['interface-abstract-add'], 0),
    ('interface-abstract-bridge', ['interface-abstract-bridge'], 0),
    ('interface-abstract-missing', ['interface-abstract-add'], 63),
    ('interface-abstract-withdraw', ['interface-abstract-original'], 0),
    ('interface-abstract-retry', ['interface-abstract-add', 'interface-abstract-add'], 0),
    ('interface-abstract-partial', ['interface-abstract-add', 'interface-abstract-add'], 63),
    ('interface-abstract-partial-retry', ['interface-abstract-add', 'interface-abstract-add', 'interface-abstract-add'], 0),
    ('interface-abstract-reject-signature', ['interface-abstract-add'], 63),
    ('interface-abstract-reject-wrong-return', ['interface-abstract-add', 'interface-abstract-wrong-return'], 63),
    ('interface-abstract-reject-private', ['interface-abstract-add', 'interface-abstract-private'], 63),
    ('interface-abstract-retry-private', ['interface-abstract-add', 'interface-abstract-add'], 0),
    ('interface-abstract-reject-package', ['interface-abstract-add', 'interface-abstract-package'], 63),
    ('interface-abstract-retry-package', ['interface-abstract-add', 'interface-abstract-add'], 0),
    ('interface-abstract-reject-protected', ['interface-abstract-add', 'interface-abstract-protected'], 63),
    ('interface-abstract-retry-protected', ['interface-abstract-add', 'interface-abstract-add'], 0),
    ('interface-abstract-reject-static', ['interface-abstract-add', 'interface-abstract-static'], 63),
    ('interface-abstract-retry-static', ['interface-abstract-add', 'interface-abstract-add'], 0),
    ('interface-abstract-reject-bridge-missing', ['interface-abstract-bridge-missing', 'interface-abstract-bridge-missing'], 63),
    ('interface-abstract-reject-conflict', ['interface-abstract-add'], 63),
    ('interface-abstract-dependent-missing', ['interface-abstract-dependent', 'interface-abstract-dependent'], 63),
    ('interface-abstract-dependent-owner', ['interface-abstract-dependent', 'interface-abstract-dependent', 'interface-abstract-dependent'], 0),
    ('interface-abstract-probe-reject', ['interface-abstract-add'], 63),
    ('interface-abstract-probe-retry', ['interface-abstract-add'], 0),
    ('interface-abstract-loader-identity', ['interface-abstract-add'], 0),
    ('interface-abstract-reabstract-reject', ['interface-abstract-reabstract'], 63),
    ('interface-abstract-reabstract-retry', ['interface-abstract-reabstract', 'interface-abstract-reabstract'], 0),
    ('interface-abstract-remove-last', ['interface-abstract-remove', 'interface-abstract-remove'], 63),
    ('interface-abstract-remove-withdraw', ['interface-abstract-add'], 0),
    ('interface-abstract-multi-group-reject', ['interface-abstract-add', 'interface-abstract-add'], 63),
    ('interface-abstract-multi-group-retry', ['interface-abstract-add', 'interface-abstract-add', 'interface-abstract-add'], 0),
    ('interface-abstract-after-plan-failure', ['interface-abstract-add', 'interface-abstract-add'], 110),
    ('interface-abstract-after-plan-retry', ['interface-abstract-add', 'interface-abstract-add'], 0),
    ('interface-abstract-late-missing', ['interface-abstract-add'], 63),
    ('interface-abstract-late-retry', ['interface-abstract-add', 'interface-abstract-add'], 0),
    ('interface-abstract-retries-exhausted', ['interface-abstract-add'], 113),
    ('interface-abstract-exhausted-withdraw', ['interface-abstract-original'], 0),
    ('interface-abstract-exhausted-retry', ['interface-abstract-add', 'interface-abstract-add', 'interface-abstract-add', 'interface-abstract-add'], 0),
    ('interface-stress-add', ['interface-stress-add'], 0),
    ("interface-hidden", ["interface-hidden"], 62),
    ("interface-class", ["interface-class"], 62),
    ("interface-missing", ["interface-missing"], 62),
    ("interface-add", ["interface-add"], 0),
    ("interface-remove", ["interface-original"], 66),
    ("super-final", ["super-final"], 62),
    ("super-interface", ["super-interface"], 62),
    ("super-missing", ["super-missing"], 62),
    ("super-final-override", ["super-final-override"], 62),
    ("super-circular", ["super-circular"], 61),
    ("super-widen", ["super-widen"], 0),
    ("super-narrow", ["super-original"], 66),
    ("super-batch-moving", ["super-batch-moving", "super-batch-moving"], 66),
    ("super-batch-child", ["super-batch-child", "super-batch-child"], 0),
    ("super-chain-new-structural", ["super-chain-structural", "super-chain-structural"], 66),
    ("super-chain-old-structural", ["super-chain-structural", "super-chain-structural"], 66),
    ("super-chain-body", ["super-chain-body", "super-chain-body", "super-chain-body"], 0),
    ("method-descriptive-flags", ["method-descriptive-flags"], 0),
    ("kind-instance", ["kind-instance"], 71),
    ("kind-instance-batch", ["kind-instance", "kind-instance"], 0),
    ("kind-private", ["kind-private"], 0),
    ("kind-static-rejected", ["kind-static"], 71),
    ("preload-subclass", ["preload-subclass"], 0),
    ("preload-hook-subclass", ["preload-hook-original"], 0),
    ("tool-add-classes", ["tool-v1", "tool-v1"], 0),
    ("tool-redefine-added", ["tool-v2"], 0),
    ("field-retire", ["field-delete"], 0),
    ("field-restore", ["field-original"], 0),
    ("field-types", ["field-types"], 0),
    ("field-retype", ["field-retype"], 0),
    ("field-retype-restore", ["field-retype-restore"], 0),
)
# Scenarios whose acceptance depends on a source-declared layout that the
# runtime probes selected: the engine's member mapping, or the runtime class
# flags of an added interface.
MAPPED_SCENARIOS = {"interface-method-bodies","interface-pointer-recovery","multi-interface-unrelated", "multi-interface-recovery",
                    "multi-interface-family", "multi-interface-shared", "multi-interface-after-shared", "multi-interface-covered",
                    "interface-overlap-uncovered","interface-overlap-body", "interface-overlap-structural", "interface-overlap-recovery","interface-implementation-body", "interface-implementation-add",
                    "interface-implementation-child", "interface-implementation-child-only",
                    "interface-implementation-retire", "interface-implementation-recovery","hierarchy", "native-change", "constructor-delete", "abstract-method-delete",
                    "clinit-delete", "methods-retire", "methods-restore", "methods-retire-again",
                    "methods-restore-again", "method-storage-delete", "method-storage-restore",
                    "nested-interface-body", "interface-after-nested",
                    "nested-interface-signature", "nested-interface-restore-signature", "nested-interface-delete",
                    "nested-interface-restore-delete", "nested-interface-field", "nested-interface-restore-field",
                    "nested-interface-initializer", "nested-interface-restore-initializer",
                    "interface-kind-default", "interface-kind-abstract", "interface-parent-add",
                    "interface-access", "interface-access-restore", "method-descriptive-flags",
                    "interface-kind-static", "interface-kind-static-restore",
                    "interface-parent-batch", "interface-parent-rebuilt",
                    "kind-instance-batch", "kind-private",
                    "interface-batch-unrelated", "interface-batch-after-rejection", "interface-batch-unrelated-reverse",
                    "interface-default-body", "copied-default-add", "interface-stress", "interface-add", "super-widen", "super-batch-child", "super-chain-body",
                    "field-retire", "field-restore", "field-types", "field-retype", "field-retype-restore"}
# VarHandle field access exists from API 33; older devices report no such checks.
MAPPED_SCENARIOS.update({'interface-members-delete', 'interface-members-signature', 'interface-miranda-add', 'interface-lambda-append', 'interface-stress-add', 'interface-bridge-add', 'interface-lambda-insert', 'interface-lambda-first', 'interface-default-add', 'interface-members-add', 'interface-miranda-prepare'})
MAPPED_SCENARIOS.update(name for name, _, code in SCENARIOS
                        if name.startswith("interface-abstract-") and code == 0)
VAR_HANDLE_API = 33
EVOLVING_CHECKS = {"oldCompute": 13, "newCompute": 13, "addedType": "long",
                   "oldAdded": 0, "newAdded": 77, "oldAddedMethod": 110,
                   "newAddedMethod": 187, "oldValue": 10, "staticCounter": 7}
FIELD_CHECKS = {"oldCompute": 41, "newCompute": 41, "instanceReferencePreserved": True,
               "staticReferencePreserved": True}
MODIFIER_CHECKS = {"valueFlags": 1, "packageValueFlags": 1, "computePublic": True,
                  "packageComputeFlags": 1, "modifierThroughPublic": 43,
                  "passiveThroughPublic": 43, "packageCompute": 13,
                  "passiveSuperclass": "com.github.ytlog.artdcevm.fixture.ModifierSubject",
                  "newPassiveThroughPublic": 43}
METHOD_BASELINE_CHECKS = {"staticBefore": 82, "directBefore": 61, "virtualBefore": 63,
                          "signatureBefore": 64, "parentBefore": 64, "interfaceBefore": 65,
                          **{kind + "Before" + str(index): value
                             for kind in ("reflection", "handle", "handleExact", "handleInvoke",
                                          "handleBound", "handleAdapted",
                                          "jni", "jniA", "jniV", "reflectedJni", "reflectedJniA",
                                          "reflectedJniV")
                             for index, value in enumerate((61, 82, 63, 64))}}
METHOD_FIELD_CHECKS = {"oldValue": 60, "oldKeep": 70, "staticKeep": 80,
                       "instanceReferencePreserved": True, "staticReferencePreserved": True,
                       "stableOldCall": 66, "newConstructorValue": 20}
METHOD_CACHED_RETIRED_CHECKS = {kind + str(index): (
                                  "java.lang.reflect.InvocationTargetException:java.lang.NoSuchMethodError"
                                  if kind == "cachedReflection" else "java.lang.NoSuchMethodError")
                              for kind in ("cachedReflection", "cachedHandle", "cachedHandleExact",
                                           "cachedHandleInvoke", "cachedHandleBound", "cachedHandleAdapted",
                                           "cachedJni", "cachedJniA", "cachedJniV",
                                           "reflectedJni", "reflectedJniA", "reflectedJniV")
                              for index in range(4)}
METHOD_RETIRE_CHECKS = {
    **METHOD_BASELINE_CHECKS, **METHOD_FIELD_CHECKS, **METHOD_CACHED_RETIRED_CHECKS,
    **{name: "java.lang.NoSuchMethodError"
       for name in ("cachedDirectCall", "cachedStaticCall", "cachedVirtualCall", "cachedSignatureCall")},
    **{"removedLookup" + str(index): "java.lang.NoSuchMethodException" for index in range(4)},
    **{"freshJniLookup" + str(index): "java.lang.NoSuchMethodError" for index in range(4)},
    "inheritedVirtualDispatch": 71, "inheritedInterfaceDispatch": 81,
    "newInheritedVirtualDispatch": 71, "newInheritedInterfaceDispatch": 81,
    "newSignatureReturnType": "long", "newSignatureOldInstance": 164,
    "newSignatureNewInstance": 124,
    "activeMethodEntered": True, "activeMethodCompleted": True, "activeMethodError": "",
    "activeMethodOldBody": 69, "removedActiveMethodLookup": "java.lang.NoSuchMethodException",
    "cachedReflectionAfterGc": "java.lang.reflect.InvocationTargetException:java.lang.NoSuchMethodError",
    "cachedHandleAfterGc": "java.lang.NoSuchMethodError", "cachedJniAfterGc": "java.lang.NoSuchMethodError",
    "threadSingleStepEnable": 0, "threadSingleStepDisable": 0, "threadSingleStepCallbacksObserved": True,
    **{name: "java.lang.NoSuchMethodError" for name in (
        "threadInterpreterDirect", "threadInterpreterStatic", "threadInterpreterVirtual",
        "threadInterpreterHandleExact", "threadInterpreterHandleInvoke", "threadInterpreterJni",
        "threadInterpreterReflectedJni")},
    "threadInterpreterReflection": "java.lang.reflect.InvocationTargetException:java.lang.NoSuchMethodError",
}
METHOD_RESTORE_CHECKS = {
    **METHOD_FIELD_CHECKS, **METHOD_CACHED_RETIRED_CHECKS,
    "restoredStaticCall": 82, "restoredDirectCall": 61, "restoredVirtualCall": 63, "restoredSignatureCall": 64,
    "restoredParentCall": 64, "restoredInterfaceCall": 65, "freshReflection": 63,
    "removedReplacementSignature": "java.lang.NoSuchMethodException",
}
CLASS_FINAL_CHECKS = {"classFlags": 1, "oldValue": 29, "newValue": 29}
INTERFACE_REJECTED_CHECKS = {"interfaceCount": 0, "contract": 51}
SUPER_REJECTED_CHECKS = {"superclass": "com.github.ytlog.artdcevm.fixture.SuperBase", "value": 12}
SUPER_WIDENED_CHECKS = {
    "superclass": "com.github.ytlog.artdcevm.fixture.SuperMiddle", "oldIsMiddle": True,
    "childIsMiddle": True, "oldOwn": 7, "oldBase": 5, "oldValue": 12, "oldMiddleValue": 0,
    "oldMiddle": 40, "oldDescribe": "middle", "childValue": 9, "childOwn": 7, "childBase": 5,
    "childDescribe": "middle"}
INTERFACE_ADDED_CHECKS = {
    "interfaces": "com.github.ytlog.artdcevm.fixture.AddedContract,java.util.function.IntSupplier,"
                  "com.github.ytlog.artdcevm.fixture.other.ContractHost$NestedContract",
    "oldIsContract": True, "childIsContract": True, "oldContractTwice": 102,
    "childContractTwice": 122, "oldIntSupplier": 52, "oldNestedDefault": 7, "childNestedDefault": 7}


def field_live_checks(i, j, o, si, so):
    """Observations through each access path while the cached fields exist."""
    return {"reflectionLiveI": i, "reflectionLiveSi": si, "jniLiveI": i, "jniLiveJ": j,
            "jniLiveO": o, "jniLiveSi": si, "jniLiveSo": so, "reflectedJniLiveI": i,
            "handleLiveI": i, "handleLiveSi": si, "varHandleLiveI": i, "varHandleLiveSi": si}


def field_retired_checks(suffix):
    """Each cached access path to a retired field and the exception it reports."""
    checks = {name + suffix: "java.lang.IllegalArgumentException"
              for name in ("reflectionI", "reflectionJ", "reflectionO", "reflectionSetI")}
    checks.update({name + suffix: "java.lang.NoClassDefFoundError"
                   for name in ("reflectionSi", "reflectionSetSo")})
    checks.update({name + suffix: "java.lang.NoSuchFieldError" for name in (
        "jni0", "jni1", "jni2", "jni3", "jni4", "jniWriteJ", "reflectedJniI", "reflectedJniSi",
        "handleGetI", "handleSetJ", "handleGetSi")})
    checks.update({name + suffix: "java.lang.UnsupportedOperationException"
                   for name in ("varHandleI", "varHandleSetI", "varHandleSi")})
    return checks


REQUIRED_CHECKS = {
    "interface-method-bodies": {"defaultBefore": 32, "staticBefore": 41, "privateBefore": 31,
        "oldDefault": 34, "newDefault": 34, "staticAfter": 42, "privateAfter": 32,
        "marker": 73, "interfaceCapability": True, "afterGc": 34},
    **{"interface-pointer-" + kind: {"arm": 0, "rejectionReason": True, "old": 51,
        "child": 51, "new": 51, "marker": 73} for kind in ("target", "subtype")},
    "interface-pointer-recovery": {"old": 52, "child": 52, "new": 52, "marker": 73},
    "multi-interface-unrelated": {"left": 12, "right": 22, "newLeft": 12, "newRight": 22,
        "leftMarker": 73, "rightMarker": 79, "allocatedOnce": 4},
    "multi-interface-invalid": {"rejectionReason": True, "leftUnchanged": 12, "rightUnchanged": 22,
        "newLeftUnchanged": 12, "newRightUnchanged": 22, "leftMarker": 73, "rightMarker": 79},
    "multi-interface-recovery": {"left": 13, "right": 23, "allocatedOnce": 4},
    "multi-interface-family": {"childArrivedFirst": 1, "parent": 32, "child": 42,
        "virtualParent": 32, "virtualChild": 42, "subclassParent": 32, "subclassChild": 42,
        "newParent": 32, "newChild": 42, "marker": 83, "subclassMarker": 89, "childMarker": 97,
        "allocatedOnce": 4, "snapshotOnce": 4, "parentAfterGc": 32, "childAfterGc": 42},
    "multi-interface-shared": {"left": 12, "right": 22, "sharedLeft": 12, "sharedRight": 22,
        "newSharedLeft": 12, "newSharedRight": 22, "sharedMarker": 37, "leftMarker": 73, "rightMarker": 79,
        "allocatedOnce": 5, "rightAfterGc": 22, "sharedRightAfterGc": 22},
    "multi-interface-after-shared": {"left": 13, "rightUnchanged": 22, "sharedLeft": 13, "sharedRight": 22},
    "multi-interface-covered": {"leftOnly": 52, "bothLeft": 52, "bothRight": 62, "newBothRight": 62,
        "leftOnlyMarker": 47, "bothMarker": 53, "allocatedOnce": 4, "snapshotOnce": 4, "bothRightAfterGc": 62},
    "interface-overlap-body": {"interfaceAfter": 32, "parentAfter": 22, "newInterface": 32,
        "newParent": 22, "marker": 73},
    "interface-overlap-structural": {"parentChanged": True, "interfaceAfter": 33, "parentAfter": 23,
        "existingAdded": 0, "newAdded": 19, "newInterface": 33, "newParent": 23, "marker": 73,
        "allocatedOnce": 3, "interfaceAfterGc": 33},
    "interface-overlap-recovery": {"interfaceAfter": 34, "parentAfter": 23, "existingAdded": 0, "marker": 73},
    "interface-overlap-superclass": {"rejectionReason": True, "superclassUnchanged": "java.lang.Object",
        "interfaceUnchanged": 34, "parentUnchanged": 23, "marker": 73},
    "interface-overlap-uncovered": {"interfaceAfter": 36, "otherAfter": 36, "parentAfter": 23,
        "existingAnother": 0, "newAnother": 29, "plainAdded": 61, "plainAnother": 0, "plainValue": 23,
        "marker": 73, "plainAfterGc": 61},
    "interface-implementation-body": {"interfaceBefore": 12, "ownBefore": 104, "implementationAllocatedFirst": 1,
        "interfaceAfter": 14, "ownAfter": 114, "childInterface": 14, "childOwn": 120,
        "newInterface": 14, "newOwn": 58, "marker": 73},
    "interface-implementation-add": {"interfaceAfter": 15, "ownAfter": 115, "marker": 73,
        "oldAdded": 0, "childAdded": 0, "newAdded": 23, "oldAddedMethod": 124, "newAddedMethod": 91},
    "interface-implementation-child": {"implementationAllocatedFirst": 1, "interfaceAfter": 16,
        "ownAfter": 116, "childInterface": 16, "childOwn": 122, "childBody": 154,
        "newChildBody": 100, "newChildInterface": 16, "newChildOwn": 60, "marker": 73, "childMarker": 79, "ownChildMarker": 83, "childAdded": 0},
    "interface-implementation-child-only": {"childBody": 155, "childInterface": 16,
        "ownAfter": 116, "childMarker": 79, "ownChildMarker": 83},
    "interface-implementation-retire": {"interfaceAfter": 17, "ownAfter": 119, "methodModifiers": 33,
        "removedMethod": "java.lang.NoSuchMethodException",
        "cachedMethod": "java.lang.reflect.InvocationTargetException:java.lang.NoSuchMethodError",
        "removedField": "java.lang.NoSuchFieldException", "cachedField": "java.lang.IllegalArgumentException",
        "marker": 73, "childInterface": 17},
    **{"interface-implementation-" + variant: {"rejectionReason": True, "interfaceUnchanged": 17,
        "ownUnchanged": 119, "childUnchanged": 17, "marker": 73, "newInterfaceUnchanged": 17}
        for variant in ("invalid", "abstract")},
    "interface-implementation-recovery": {"interfaceAfter": 18, "ownAfter": 119,
        "childInterface": 18, "marker": 73, "afterGc": 18},
    "interface-batch-unrelated-reverse": {"interfaceAfter": 12, "unrelatedAfter": 22,
        "interfaceMarker": 73, "unrelatedMarker": 79},
    "interface-batch-unrelated": {"interfaceBefore": 11, "unrelatedBefore": 21,
        "interfaceAfter": 12, "virtualAfter": 12, "unrelatedAfter": 22,
        "newInterface": 12, "newUnrelated": 22, "interfaceMarker": 73, "unrelatedMarker": 79},
    "interface-batch-unrelated-invalid": {"rejectionReason": True, "interfaceUnchanged": 12,
        "unrelatedUnchanged": 22, "newInterfaceUnchanged": 12, "newUnrelatedUnchanged": 22,
        "interfaceMarker": 73, "unrelatedMarker": 79},
    "interface-batch-after-rejection": {"interfaceAfter": 13, "unrelatedUnchanged": 22,
        "interfaceMarker": 73, "afterGc": 13},
    "interface-stress": {
        "baseline": 1400, "snapshotTypes": 207, "snapshotMeasured": True,
        "allocationMeasured": True, "runtimeProbed": True,
        **{f"{kind}{i}": (i if kind == "oldMarker" else 17 if kind == "newMarker" else 8)
           for i in range(200) for kind in ("oldDispatch", "oldMarker", "newDispatch", "newMarker", "dispatchAfterGc")},
    },
    "body": {"computeBefore": 11, "oldCompute": 12, "newCompute": 12},
    "add": EVOLVING_CHECKS,
    "method-delete": {"oldCompute": 14, "newCompute": 14, "oldValue": 10,
                       "staticCounter": 7, "removedLookup": "java.lang.NoSuchMethodException",
                       "cachedMethod": "java.lang.reflect.InvocationTargetException:java.lang.NoSuchMethodError"},
    "method-restore": EVOLVING_CHECKS,
    "hierarchy": {"superclass": "java.util.ArrayList", "interfaces": "java.lang.Runnable",
                  "oldValue": 10, "oldCompute": 16, "oldIsList": True, "oldListSize": 0,
                  "oldRunnable": 11, "retiredField": "java.lang.NoSuchFieldException",
                  "newCompute": 9, "staticCounter": 7},
    "atomic-modifier-public-class-interface": {
        **CLASS_FINAL_CHECKS, "modifierBefore": 42, "passiveBefore": 42,
        "modifierThroughPublic": 42, "passiveThroughPublic": 42,
        "modifierFinalizable": 29, "passiveFinalizable": 29},
    "modifier-public": {**MODIFIER_CHECKS, "computeFlags": 1, "finalizableFlags": 1,
                        "modifierFinalizable": 29, "passiveFinalizable": 29},
    "modifier-final": {**MODIFIER_CHECKS, "computeFlags": 1, "finalizableFlags": 17,
                       "modifierFinalizable": 30, "passiveFinalizable": 30},
    "modifier-sync": {**MODIFIER_CHECKS, "computeFlags": 33, "finalizableFlags": 1,
                      "modifierFinalizable": 29, "passiveFinalizable": 29},
    "modifier-field-final": {"oldValueBefore": 61, "staticValueBefore": 72,
                             "valueFlags": 17, "staticValueFlags": 25, "oldValue": 61,
                             "oldRead": 61, "staticValue": 72, "staticRead": 72,
                             "newConstructorValue": 41},
    "class-final": {"finalFlags": 17, "oldValue": 30, "newValue": 30},
    "class-nonfinal": CLASS_FINAL_CHECKS,
    "class-abstract-instances": CLASS_FINAL_CHECKS,
    "class-abstract": {"candidateBefore": 33, "candidateFlags": 1024, "candidateAfter": 34},
    "class-final-subclassed": {"subclassedFlags": 1, "passiveStillSubclass": True},
    "atomic-method-retire-class-interface": {
        **METHOD_BASELINE_CHECKS, **CLASS_FINAL_CHECKS,
        "unchangedStaticCall": 82, "unchangedVirtualCall": 63, "unchangedSignatureCall": 64},
    "native-change": {
        "other": 6, "toJavaCall": 95, "toJavaCachedReflection": 95, "toJavaIsNative": False,
        "removedNativeLookup": "java.lang.NoSuchMethodException",
        "removedNativeCachedReflection":
            "java.lang.reflect.InvocationTargetException:java.lang.NoSuchMethodError",
        "removedNativeCall": "java.lang.NoSuchMethodError"},
    "constructor-delete": {
        "jniBefore": 4, "oldValue": 9, "newDefault": 2,
        "removedLookup": "java.lang.NoSuchMethodException",
        "cachedConstructor": "java.lang.reflect.InvocationTargetException:java.lang.NoSuchMethodError",
        "cachedJni": "java.lang.NoSuchMethodError", "bytecodeNew": "java.lang.NoSuchMethodError"},
    "abstract-method-delete": {
        "callBefore": 19, "ownValue": 19, "concreteValue": 18, "staticMarker": 17,
        "removedLookup": "java.lang.NoSuchMethodException",
        "cachedReflection": "java.lang.reflect.InvocationTargetException:java.lang.NoSuchMethodError",
        "callThroughRemoved": "java.lang.NoSuchMethodError"},
    "abstract-method-make-abstract": {"concreteIsAbstract": True,
                                      "unimplemented": "java.lang.AbstractMethodError", "ownValue": 19},
    "abstract-method-restore": {"concreteValue": 17, "callRestored": 19, "newChild": 17},
    "clinit-delete": {"valueBefore": 41, "valueAfter": 41, "computeAfter": 41},
    "methods-retire": METHOD_RETIRE_CHECKS,
    "methods-restore": METHOD_RESTORE_CHECKS,
    "methods-retire-again": METHOD_RETIRE_CHECKS,
    "methods-restore-again": METHOD_RESTORE_CHECKS,
    "method-private": {"callerBefore": 7, "targetIsPublic": True, "callerAfter": 7},
    "method-storage": {"callerStaticBefore": 32, "callerVirtualBefore": 8, "staticValueIsStatic": True,
                       "callerStatic": 32, "callerVirtual": 8, "combined": 40},
    "method-storage-delete": {
        "combined": 35, "callerStatic": "java.lang.NoSuchMethodError",
        "callerVirtual": "java.lang.NoSuchMethodError",
        "cachedReflectionStatic": "java.lang.reflect.InvocationTargetException:java.lang.NoSuchMethodError"},
    "method-storage-readd": {
        "combined": 35, "callerStatic": "java.lang.NoSuchMethodError",
        "callerVirtual": "java.lang.NoSuchMethodError"},
    "method-storage-restore": {
        "combined": 40, "callerStatic": 32, "callerVirtual": 8,
        "cachedReflectionStatic": "java.lang.reflect.InvocationTargetException:java.lang.NoSuchMethodError"},
    "nested-interface-body": {"innerStatic": True, "before": 41, "oldInterface": 42,
                              "oldVirtual": 42, "newInstance": 42, "marker": 73, "afterGc": 42},
    **{"nested-interface-" + variant: {"unchanged": 42, "unchangedVirtual": 42,
        "unchangedNew": 42, "marker": 73, "rejectionReason": True, "runtimeAvailable": True}
       for variant in ("add", "modifiers", "parents")},
    "nested-interface-signature": {"newSignature": 45, "newSignatureVirtual": 46,
        "oldInterfaceCall": "java.lang.NoSuchMethodError", "oldVirtualCall": "java.lang.NoSuchMethodError",
        "oldReflection": "java.lang.NoSuchMethodError", "marker": 73, "newSignatureAfterGc": 45},
    "nested-interface-restore-signature": {"restored": 42, "restoredVirtual": 42,
        "retiredReflection": "java.lang.NoSuchMethodError", "marker": 73},
    "nested-interface-delete": {"declaredMethods": 0, "interfaceCall": "java.lang.NoSuchMethodError",
        "virtualCall": "java.lang.NoSuchMethodError", "newInstanceCall": "java.lang.NoSuchMethodError", "marker": 73},
    "nested-interface-restore-delete": {"restored": 42, "restoredVirtual": 42, "restoredNew": 42, "marker": 73},
    "nested-interface-field": {"addedField": 0, "value": 44, "marker": 73},
    "nested-interface-restore-field": {"fieldRetired": 0, "restored": 42, "marker": 73},
    "nested-interface-initializer": {"initializerNotRun": True, "value": 47, "marker": 73},
    "nested-interface-restore-initializer": {"fieldRetired": 0, "restored": 42, "marker": 73},
    "interface-kind-default": {"requiredBefore": 41, "required": 12, "virtualRequired": 12, "newRequired": 12,
        "optional": 11, "defaultFlag": True, "marker": 7},
    "interface-kind-abstract-missing": {"rejectionReason": True, "optionalUnchanged": 11,
        "defaultUnchanged": True, "marker": 7},
    "interface-kind-abstract": {"optional": 51, "virtualOptional": 51, "abstractFlag": True, "required": 12,
        "marker": 7, "optionalAfterGc": 51},
    "interface-parent-missing": {"rejectionReason": True, "notParent": False, "value": 11, "marker": 9},
    "interface-parent-add": {"contractExtends": True, "instanceIsParent": True, "parentRequired": 41,
        "parentDefault": 21, "completeRequired": 31, "completeDefault": 21, "newRequired": 41, "value": 12,
        "marker": 9, "parentAfterGc": 41},
    "interface-parent-remove": {"rejectionReason": True, "stillParent": True, "value": 12},
    "interface-parent-batch": {"plainIsParent": True, "plainRequired": 51, "plainDefault": 22,
        "implementationDefault": 22, "plainDefaultAfterGc": 22},
    "interface-parent-rebuilt": {"extraLoaded": True, "instanceIsExtra": True, "extraDefault": 32, "value": 13,
        "marker": 9},
    "interface-after-nested": {"topLevelDispatch": 93, "nestedDispatch": 42, "runtimeAvailable": True},
    "interface-default-body": {"oldChildInterface": 93, "oldChildVirtual": 93,
                               "newChild": 93, "parentValue": 90},
    "copied-default-add": {"copiedBefore": 93, "oldChildInterface": 92, "oldChildVirtual": 92,
                           "newChild": 92, "parentValue": 90},
    "interface-hidden": INTERFACE_REJECTED_CHECKS,
    "interface-class": INTERFACE_REJECTED_CHECKS,
    "interface-missing": INTERFACE_REJECTED_CHECKS,
    "interface-add": {**INTERFACE_ADDED_CHECKS, "contractLoadedBefore": False,
                      "nestedLoadedBefore": False, "contractLoadedAfter": True,
                      "newContractTwice": 102},
    "interface-remove": INTERFACE_ADDED_CHECKS,
    **{name: SUPER_REJECTED_CHECKS for name in (
        "super-final", "super-interface", "super-missing", "super-final-override", "super-circular")},
    "super-widen": {**SUPER_WIDENED_CHECKS, "describeBefore": "base", "newMiddleValue": 11,
                    "newValue": 12},
    "super-narrow": SUPER_WIDENED_CHECKS,
    "super-batch-moving": {"rejectionReason": True,
        "superclassUnchanged": "com.github.ytlog.artdcevm.fixture.SuperBatchFixture$Base", "base": 19, "childValue": 13},
    **{"super-chain-" + variant: {"rejectionReason": True,
        "superclassUnchanged": "com.github.ytlog.artdcevm.fixture.SuperChainFixture$Base", "base": 19}
       for variant in ("new-structural", "old-structural")},
    "super-chain-body": {"middleIdentity": True, "existingIsMiddle": True, "existingChain": 87, "newChain": 74,
        "publishedMiddle": 61, "publishedBase": 6, "own": 17, "existingChainAfterGc": 87},
    "kind-instance": {"rejectionReason": True, "callerUnchanged": 2, "staticUnchanged": 2},
    "kind-instance-batch": {"instanceKind": False, "caller": 3, "instanceCall": 4,
        "retiredStatic": "java.lang.NoSuchMethodError", "marker": 9, "callerAfterGc": 3},
    "kind-private": {"privateKind": True, "useHelper": 6, "marker": 9},
    "kind-static-rejected": {"rejectionReason": True, "callerUnchanged": 3},
    "method-descriptive-flags": {"varargsBefore": True, "varargsAfter": False, "strictAfter": False,
        "sum": 7, "half": 2.5},
    "interface-access": {"publicAfter": False, "value": 2, "marker": 3},
    "interface-access-restore": {"publicAfter": True, "value": 3, "marker": 3},
    "interface-kind-static-rejected": {"rejectionReason": True, "callerUnchanged": 11},
    "interface-kind-static": {"valueStatic": True, "staticValue": 21, "caller": 22,
        "retiredDefault": "java.lang.NoSuchMethodError", "twiceDefault": True, "twice": 12, "helperPrivate": True,
        "useHelper": 7, "newImpl": 12, "marker": 9, "callerAfterGc": 22},
    "interface-kind-static-restore": {"valueDefault": True, "caller": 13, "twiceStatic": 8, "useHelper": 7, "marker": 9},
    "super-batch-child": {"subjectSuperclass": "com.github.ytlog.artdcevm.fixture.SuperBatchFixture$Middle",
        "childIsMiddle": True, "existingViaMiddle": 53, "newViaMiddle": 60, "existingMiddleValue": 0,
        "own": 17, "baseValue": 19, "childValue": 13, "viaMiddleAfterGc": 53},
    "preload-subclass": {"childLoadedBefore": False, "childLoadedAfter": True, "childLayout": 435},
    "preload-hook-subclass": {"childLoadedBefore": False, "hookInstalled": 0, "hookReplacements": 2,
                              "subclassLoadRetries": 1, "childLoadedAfter": True, "childLayout": 435},
    "tool-add-classes": {
        "addedLoadedBefore": False, "dormantLoadedBefore": False,
        "classified": "added=ToolAdded,ToolChild,ToolHost$$ExternalSyntheticLambda0;changed=ToolDormant,ToolHost",
        "appendCode": 0, "subclassLoadRetries": 0, "run": 1443, "oldOwn": 6, "oldDescribe": 1,
        "dormantValue": 2},
    "tool-redefine-added": {"classified": "added=;changed=ToolAdded", "appendCode": -1, "run": 1543},
    "field-retire": {
        **FIELD_CHECKS, **field_live_checks(7, 9_000_000_000, 1, 7, 1),
        "keptInstance": 43, "keptStatic": 44, "retiredOldCompute": 44, "retiredNewCompute": 42,
        "remainingFields": "[keep, staticKeep]", "keptAfterGc": 43,
        **{name: "java.lang.NoSuchFieldError" for name in (
            "bytecodeReadI", "bytecodeWriteJ", "bytecodeReadO", "bytecodeReadSi", "bytecodeWriteSo")},
        **field_retired_checks(""), **field_retired_checks("AfterGc")},
    "field-restore": {
        "restoredInstanceDefault": 0, "restoredReferenceDefault": True, "restoredBooleanDefault": False,
        "restoredStaticDefault": 0, "restoredStaticReferenceDefault": True,
        "keptInstance": 43, "keptStatic": 44, "freshReflection": 0,
        **field_retired_checks("Cached"), **field_live_checks(17, 18, 1, 19, 1)},
    "field-types": {
        "typeChangedI": "long", "typeChangedDefault": 0, "storageChangedSi": "java.lang.Object",
        "storageChangedDefault": True, "keptInstance": 43, "oldCompute": 45,
        "bytecodeOldTypeI": "java.lang.NoSuchFieldError", "bytecodeOldTypeSi": "java.lang.NoSuchFieldError",
        **field_retired_checks("Restored")},
    "field-retype": {
        "textType": "java.lang.CharSequence", "keep": 41, "childOwn": 17,
        **{name: True for name in ("textKept", "numberKept", "mismatchCleared", "interfaceKept", "staticKept",
                                   "staticMismatchCleared", "childTextKept", "childInterfaceKept",
                                   "textKeptAfterGc", "childInterfaceKeptAfterGc")}},
    "field-retype-restore": {
        "textType": "java.lang.String",
        **{name: True for name in ("builderCleared", "childTextKept", "numberKept", "interfaceKept",
                                   "staticKept", "mismatchNull", "staticMismatchNull")}},
}
# Interface direct targets retain exact old reflection objects, handles and
# JNI IDs, and old bytecode wrappers still name the removed declarations.
INTERFACE_DIRECT_RETIRED = {
    **{kind + str(i) + suffix: error
       for i in range(2) for suffix in ("", "AfterGc")
       for kind, error in (("lookup", "java.lang.NoSuchMethodException"),
                           ("jniLookup", "java.lang.NoSuchMethodError"),
                           ("reflection", "java.lang.reflect.InvocationTargetException:java.lang.NoSuchMethodError"),
                           ("handle", "java.lang.NoSuchMethodError"))},
    **{kind + str(i) + str(form) + suffix: "java.lang.NoSuchMethodError"
       for i in range(2) for form in range(3) for suffix in ("", "AfterGc")
       for kind in ("jni", "reflectedJni")},
    "bytecodeDirect": "java.lang.NoSuchMethodError", "bytecodeStatic": "java.lang.NoSuchMethodError",
    "oldDispatch": 63, "afterGc": 63,
}
REQUIRED_CHECKS.update({
    "interface-members-add": {"oldAdded": 110, "newAdded": 110, "oldDispatch": 63, "marker": 73,
        "afterGc": 110, "staticAdded": 10, "publicAdded": 12, "packageAdded": 13,
        "protectedAdded": 14, "syntheticAdded": 15, "staticAddedFlags": 10,
        "publicAddedFlags": 9, "packageAddedFlags": 8, "protectedAddedFlags": 12,
        "syntheticAddedFlags": 0x1009},
    "interface-members-signature": {**INTERFACE_DIRECT_RETIRED, "newDirect": 161, "newStatic": 182, "marker": 73},
    "interface-members-delete": {**INTERFACE_DIRECT_RETIRED, "newDispatch": 63},
    "interface-default-add": {"before": 20, "copiedDefault": 60, "newCopiedDefault": 60,
        "subinterfaceWins": 70, "publicClassWins": 80, "overridesParent": 50, "oldDispatch": 1,
        "marker": 79, "childMarker": 83, "afterGc": 60, "childAfterGc": 70},
    "interface-miranda-prepare": {"before": 9, "miranda": "java.lang.AbstractMethodError", "stable": 7},
    "interface-miranda-add": {"oldMiranda": 99, "newMiranda": 99, "stable": 7, "afterGc": 99},
    "interface-default-conflict": {"before": 21, "rejectionReason": True, "oldDispatch": 21,
        "oldLeft": 11, "newDispatch": 21, "marker": 89, "afterGc": 21},
    **{"interface-hide-parent-" + kind: {"rejectionReason": True, "oldDispatch": 31,
        "newDispatch": 31, "afterGc": 31} for kind in ("static", "private")},
    "interface-hide-object": {"rejectionReason": True, "oldDispatch": 41, "newDispatch": 41, "afterGc": 41},
    **{"interface-shadow-" + kind: {"rejectionReason": True, "oldDispatch": 51 + 10 * i,
        "newDispatch": 51 + 10 * i, "oldShadow": 52 + 10 * i, "afterGc": 51 + 10 * i}
        for i, kind in enumerate(("static", "private", "protected", "package"))},
    "interface-bridge-add": {"before": 90, "oldBridge": 90, "newBridge": 90,
        "oldAdded": 91, "newAdded": 91, "hasBridge": True, "addedBridge": True, "afterGc": 91},
    **{"interface-lambda-" + kind: {"appendCode": 0, "redefinedTypes": i + 1,
        "oldDispatch": value, "newDispatch": value, "marker": 97, "afterGc": value,
        **({"callerAppendCode": 0, "callerExistingTypes": 1, "bytecodeBridge": 5} if i == 1 else {}),
        **({"retiredMethods": 2, "cachedStaticTargets": 4, "bytecodeBefore": 5,
            "bytecodeBridge": "java.lang.reflect.InvocationTargetException:java.lang.NoSuchMethodError",
            "bytecodeBridgeAfterGc": "java.lang.reflect.InvocationTargetException:java.lang.NoSuchMethodError",
            **{"liveStaticTarget" + str(target): True for target in range(4)}, **{kind + suffix: 1
            for kind in ("retiredPrivate", "retiredSynthetic") for suffix in ("", "AfterGc")},
            **{"retired" + access + kind + suffix: ("java.lang.reflect.InvocationTargetException:java.lang.NoSuchMethodError"
                if access == "Reflection" else "java.lang.NoSuchMethodError")
                for access in ("Reflection", "Handle", "Jni") for kind in ("Private", "Synthetic")
                for suffix in ("", "AfterGc")}} if i == 2 else {})}
        for i, (kind, value) in enumerate((("first", 5), ("append", 11), ("insert", 111)))},
    "interface-stress-add": {"snapshotTypes": 207,
        **{kind + str(i): (i if kind == "oldMarker" else 8 if kind == "oldDispatch" else 18)
            for i in range(200) for kind in ("oldAdded", "newAdded", "oldDispatch", "oldMarker", "afterGc")}},
})
UNCHANGED_CHECKS = {
    "interface-default-conflict": ("unchangedInterface", "unchangedImplementation"),
    **{name: ("unchangedMembers",) for name in ['interface-hide-parent-static', 'interface-hide-parent-private', 'interface-shadow-static', 'interface-shadow-private', 'interface-shadow-protected', 'interface-shadow-package']},
    "interface-overlap-superclass": ("unchangedParent",),
    **{name: ("unchangedSubject",) for name in ("kind-instance", "kind-static-rejected")},
    "interface-kind-static-rejected": ("unchangedContract",),
    **{name: ("unchangedSubject",) for name in ("super-batch-moving", "super-chain-new-structural",
                                                 "super-chain-old-structural")},
    **{"interface-implementation-" + variant: ("unchangedInterface", "unchangedImplementation")
       for variant in ("invalid", "abstract")},
    "interface-batch-unrelated-invalid": ("unchangedInterface", "unchangedUnrelated"),
    **{"nested-interface-" + variant: ("unchangedMembers",)
       for variant in ("add", "modifiers", "parents")},
    **{"interface-parent-" + variant: ("unchangedMembers",) for variant in ("missing", "remove")},
    "atomic-modifier-public-class-interface": ("unchangedModifierMembers", "unchangedMembers"),
    "class-nonfinal": ("unchangedMembers",),
    "class-abstract-instances": ("unchangedMembers",),
    "class-final-subclassed": ("unchangedMembers",),
    "atomic-method-retire-class-interface": ("unchangedMethodMembers", "unchangedMembers"),
    "field-retire": ("unchangedMembers", "unchangedValues"),
    "interface-hidden": ("unchangedMembers",),
    "interface-class": ("unchangedMembers",),
    "interface-missing": ("unchangedMembers",),
    "interface-add": ("unchangedMembers",),
    **{name: ("unchangedMembers",) for name in (
        "super-final", "super-interface", "super-missing", "super-final-override", "super-circular",
        "super-widen")},
}


class RunFailure(RuntimeError):
    """A safe failure message; raw command details stay in local evidence."""


REQUIRED_CHECKS.update({'interface-abstract-dependent-missing': {'methodNamed': True,
                                          'classNamed': True,
                                          'missingImplementation': True,
                                          'oldBody': 11},
 'interface-abstract-reject-conflict': {'classNamed': True,
                                        'missingImplementation': True,
                                        'oldBody': 11,
                                        'noDeclaration': 'java.lang.NoSuchMethodException'},
 'interface-abstract-probe-reject': {'armed': 0,
                                     'probeReason': True,
                                     'capabilityReason': True,
                                     'oldBody': 11},
 'interface-abstract-probe-retry': {'newCaller': 'existing'},
 'interface-abstract-batch-forward': {'newCaller': 'new',
                                      'singleton': True,
                                      'marker': 73,
                                      'counter': 79,
                                      'bodyUpdated': 'new',
                                      'dispatchProbe': True,
                                      'afterGc': 'new'},
 'interface-abstract-batch-reverse': {'newCaller': 'new',
                                      'singleton': True,
                                      'marker': 73,
                                      'counter': 79,
                                      'bodyUpdated': 'new',
                                      'dispatchProbe': True,
                                      'afterGc': 'new'},
 'interface-abstract-existing': {'implementation': 'existing', 'interfaceBody': 12},
 'interface-abstract-inherited': {'implementation': 'parent', 'interfaceBody': 12},
 'interface-abstract-defaultimplementation': {'implementation': 'default', 'interfaceBody': 12},
 'interface-abstract-bridge': {'implementation': 'bridge', 'interfaceBody': 12},
 'interface-abstract-missing': {'methodNamed': True,
                                'classNamed': True,
                                'missingImplementation': True,
                                'oldBody': 11,
                                'noDeclaration': 'java.lang.NoSuchMethodException'},
 'interface-abstract-withdraw': {'oldBody': 11},
 'interface-abstract-retry': {'newCaller': 'new'},
 'interface-abstract-partial': {'methodNamed': True,
                                'classNamed': True,
                                'missingImplementation': True,
                                'oldBody': 11,
                                'implementationUnchanged': 'old',
                                'noDeclaration': 'java.lang.NoSuchMethodException'},
 'interface-abstract-partial-retry': {'newCaller': 'new', 'other': 'other'},
 'interface-abstract-reject-signature': {'classNamed': True,
                                         'missingImplementation': True,
                                         'oldBody': 11,
                                         'noDeclaration': 'java.lang.NoSuchMethodException'},
 'interface-abstract-reject-wrong-return': {'classNamed': True,
                                            'missingImplementation': True,
                                            'oldBody': 11,
                                            'noDeclaration': 'java.lang.NoSuchMethodException'},
 'interface-abstract-reject-private': {'classNamed': True,
                                       'missingImplementation': True,
                                       'oldBody': 11,
                                       'noDeclaration': 'java.lang.NoSuchMethodException'},
 'interface-abstract-reject-package': {'classNamed': True,
                                       'missingImplementation': True,
                                       'oldBody': 11,
                                       'noDeclaration': 'java.lang.NoSuchMethodException'},
 'interface-abstract-reject-protected': {'classNamed': True,
                                         'missingImplementation': True,
                                         'oldBody': 11,
                                         'noDeclaration': 'java.lang.NoSuchMethodException'},
 'interface-abstract-reject-static': {'classNamed': True,
                                      'missingImplementation': True,
                                      'oldBody': 11,
                                      'noDeclaration': 'java.lang.NoSuchMethodException'},
 'interface-abstract-reject-bridge-missing': {'classNamed': True,
                                              'missingImplementation': True,
                                              'oldBody': 11,
                                              'noDeclaration': 'java.lang.NoSuchMethodException'},
 'interface-abstract-dependent-owner': {'dependent': 'dependent',
                                        'ownerBody': 12,
                                        'siblingBody': 12},
 'interface-abstract-loader-identity': {'newCaller': 'existing',
                                        'otherLoaderOldBody': 11,
                                        'otherLoaderNoDeclaration': 'java.lang.NoSuchMethodException'}})
REQUIRED_CHECKS.update({'interface-abstract-reabstract-reject': {'methodNamed': True,
                                          'classNamed': True,
                                          'missingImplementation': True,
                                          'oldDefault': 'distant',
                                          'oldJni': 'distant'},
 'interface-abstract-reabstract-retry': {'newImplementation': 'implemented',
                                         'cachedDefaultCall': 'implemented',
                                         'cachedDefaultReflection': 'implemented',
                                         'cachedDefaultJni': 'implemented'},
 'interface-abstract-remove-last': {'methodNamed': True,
                                    'classNamed': True,
                                    'missingImplementation': True,
                                    'oldReflection': 'removable',
                                    'oldJni': 'removable'},
 'interface-abstract-remove-withdraw': {'newCaller': 'removable', 'cachedJni': 'removable'},
 'interface-abstract-multi-group-reject': {'methodNamed': True,
                                           'classNamed': True,
                                           'missingImplementation': True,
                                           'earlierGroupValidated': True,
                                           'firstReflection': 11,
                                           'firstHandle': 11,
                                           'firstBytecode': 11,
                                           'firstJni': 11,
                                           'secondReflection': 11,
                                           'secondHandle': 11,
                                           'secondBytecode': 11,
                                           'secondJni': 11,
                                           'firstUnchanged': 'java.lang.NoSuchMethodException'},
 'interface-abstract-multi-group-retry': {'firstReflection': 12,
                                          'firstHandle': 12,
                                          'firstBytecode': 12,
                                          'firstJni': 12,
                                          'secondReflection': 12,
                                          'secondHandle': 12,
                                          'secondBytecode': 12,
                                          'secondJni': 12,
                                          'firstNewCaller': 'existing',
                                          'secondNewCaller': 'new'},
 'interface-abstract-after-plan-failure': {'armed': 0,
                                           'planned': True,
                                           'planFailure': True,
                                           'oldReflection': 11,
                                           'oldHandle': 11,
                                           'oldBytecode': 11,
                                           'oldJni': 11,
                                           'oldImplementation': 'old'},
 'interface-abstract-after-plan-retry': {'newReflection': 12,
                                         'newHandle': 12,
                                         'newBytecode': 12,
                                         'newJni': 12,
                                         'newCaller': 'new'},
 'interface-abstract-late-missing': {'hook': 0,
                                     'methodNamed': True,
                                     'classNamed': True,
                                     'missingImplementation': True,
                                     'retried': 1,
                                     'oldReflection': 11,
                                     'oldHandle': 11,
                                     'oldBytecode': 11,
                                     'oldJni': 11},
 'interface-abstract-late-retry': {'newReflection': 12,
                                   'newHandle': 12,
                                   'newBytecode': 12,
                                   'newJni': 12,
                                   'existing': 'existing',
                                   'other': 'other'},
 'interface-abstract-retries-exhausted': {'hook': 0,
                                          'boundedRetries': 2,
                                          'reason': True,
                                          'oldReflection': 11,
                                          'oldHandle': 11,
                                          'oldBytecode': 11,
                                          'oldJni': 11},
 'interface-abstract-exhausted-withdraw': {'oldReflection': 11,
                                           'oldHandle': 11,
                                           'oldBytecode': 11,
                                           'oldJni': 11},
 'interface-abstract-exhausted-retry': {'newReflection': 12,
                                        'newHandle': 12,
                                        'newBytecode': 12,
                                        'newJni': 12,
                                        'existing': 'existing',
                                        'lateOne': 'late-one'},
 'interface-abstract-retry-private': {'newCaller': 'new'},
 'interface-abstract-retry-package': {'newCaller': 'new'},
 'interface-abstract-retry-protected': {'newCaller': 'new'},
 'interface-abstract-retry-static': {'newCaller': 'new'},
 'interface-abstract-batch-forward': {'newCaller': 'new',
                                      'singleton': True,
                                      'marker': 73,
                                      'counter': 79,
                                      'bodyUpdated': 'new',
                                      'dispatchProbe': True,
                                      'afterGc': 'new',
                                      'abstractCapability': True,
                                           'sameBatchCaller': 'new/12',
                                      'cachedReflection': 12,
                                      'cachedHandle': 12,
                                      'cachedBytecode': 12,
                                      'cachedJni': 12},
 'interface-abstract-batch-reverse': {'newCaller': 'new',
                                      'singleton': True,
                                      'marker': 73,
                                      'counter': 79,
                                      'bodyUpdated': 'new',
                                      'dispatchProbe': True,
                                      'afterGc': 'new',
                                      'abstractCapability': True,
                                           'sameBatchCaller': 'new/12',
                                      'cachedReflection': 12,
                                      'cachedHandle': 12,
                                      'cachedBytecode': 12,
                                      'cachedJni': 12}})

def scenario_sequence(scenario_group):
    if scenario_group == "full":
        return SCENARIOS
    if scenario_group == "interface-abstract":
        return tuple(item for item in SCENARIOS if item[0].startswith("interface-abstract-"))
    if scenario_group == "ordinary":
        # Match the contiguous interface-target block in runScenarios. Ordinary
        # classes adding interfaces remain part of the ordinary suite.
        names = [item[0] for item in SCENARIOS]
        return SCENARIOS[:names.index("interface-method-bodies")] + SCENARIOS[names.index("interface-hidden"):]
    raise RunFailure("Unsupported scenario group")


def validate_report(report, scenario_group="full"):
    """Require every requested return code and an actual behavior observation."""
    if (not isinstance(report, dict) or type(report.get("schemaVersion")) is not int
            or report["schemaVersion"] != 1):
        raise RunFailure("The device report has an unsupported schema")
    if (report.get("completed") is not True or type(report.get("failures")) is not int
            or report["failures"] != 0):
        raise RunFailure("The device report is incomplete or reports failures")
    if type(report.get("uid")) is not int or report["uid"] < 10000:
        raise RunFailure("Instrumentation did not run under an application UID")
    if report.get("fatalError"):
        raise RunFailure("Instrumentation reported a fatal error")
    device = report.get("device")
    if (not isinstance(device, dict) or type(device.get("api")) is not int
            or device["api"] < 30 or not isinstance(device.get("model"), str)
            or not device["model"] or not isinstance(device.get("release"), str)
            or not device["release"]):
        raise RunFailure("The device report is missing device identity metadata")
    initialization = report.get("initialization")
    if (not isinstance(initialization, dict) or initialization.get("passed") is not True
            or initialization.get("runtimeInitialized") is not True
            or initialization.get("clientAttached") is not True):
        raise RunFailure("The structural engine did not initialize")
    capabilities = initialization.get("capabilities")
    if not isinstance(capabilities, dict):
        raise RunFailure("The initialization report is missing engine capabilities")
    for name in ("installed", "standardRedefineRouted", "memberAddition", "memberModifierChange",
                 "classModifierChange", "interfaceAddition", "superclassWidening", "methodDeletion", "methodSignatureChange", "fieldDeletion", "fieldTypeChange",
                 "memberDeletion", "memberTypeChange"):
        if capabilities.get(name) is not True:
            raise RunFailure("The initialization report is missing a required capability")
    interface_runtime = capabilities.get("interfaceRuntime")
    interface_pending = interface_runtime == "unverified: interface runtime declarations"
    interface_disabled = (scenario_group == "ordinary" and isinstance(interface_runtime, str)
                          and interface_runtime.startswith("disabled: "))
    if (capabilities.get("interfaceRedefinition") is not False or
            not (interface_pending or interface_disabled)):
        raise RunFailure("Interface capability must stay false until runtime probes pass")
    for name in ("hierarchyChange", "fullDcevmComplete"):
        if capabilities.get(name) is not False:
            raise RunFailure("The engine capabilities claim unimplemented hierarchy changes")
    expected_scenarios = scenario_sequence(scenario_group)
    scenarios = report.get("scenarios")
    if not isinstance(scenarios, list) or len(scenarios) != len(expected_scenarios):
        raise RunFailure("The device report does not contain the complete scenario sequence")
    for scenario, (name, variants, code) in zip(scenarios, expected_scenarios):
        if not isinstance(scenario, dict) or scenario.get("name") != name:
            raise RunFailure("The device report has an unexpected scenario order")
        if scenario.get("variants") != variants:
            raise RunFailure("Scenario " + name + " did not submit the requested variants")
        if (type(scenario.get("expectedCode")) is not int or scenario["expectedCode"] != code
                or type(scenario.get("actualCode")) is not int or scenario["actualCode"] != code):
            raise RunFailure("Scenario " + name + " returned an unexpected JVMTI code")
        if (scenario.get("completed") is not True or scenario.get("passed") is not True
                or scenario.get("error")):
            raise RunFailure("Scenario " + name + " failed")
        if name == "interface-stress":
            timing = scenario.get("interfaceTiming")
            if (not isinstance(timing, dict) or type(timing.get("types")) is not int
                    or timing["types"] != 207 or any(type(timing.get(key)) is not int
                    or timing[key] <= 0 for key in ("snapshotNanos", "allocationNanos"))):
                raise RunFailure("Interface stress is missing measured phase timing or the complete type count")
        scenario_capabilities = scenario.get("capabilities") or {}
        verified_interface = str(scenario_capabilities.get("interfaceRuntime", "")).startswith("verified: ")
        if scenario_capabilities.get("interfaceRedefinition") is not verified_interface:
            raise RunFailure("Interface capability disagrees with runtime probe state")
        mapping = (scenario.get("capabilities") or {}).get("memberMapping")
        if name in MAPPED_SCENARIOS and not (isinstance(mapping, str)
                                             and mapping.startswith("verified: ")):
            raise RunFailure("Scenario " + name + " ran without a runtime-verified member mapping")
        checks = scenario.get("checks")
        if not isinstance(checks, list) or not checks:
            raise RunFailure("Scenario " + name + " is missing behavior checks")
        names = set()
        for index, check in enumerate(checks):
            if not isinstance(check, dict) or not isinstance(check.get("name"), str) or not check["name"]:
                raise RunFailure("Scenario " + name + " has a malformed behavior check")
            if check["name"] in names:
                raise RunFailure("Scenario " + name + " repeats a behavior check")
            names.add(check["name"])
            if (check.get("passed") is not True or "expected" not in check or "actual" not in check
                    or type(check["expected"]) is not type(check["actual"])
                    or check["expected"] != check["actual"]):
                raise RunFailure("Scenario " + name + " behavior check " + str(index + 1) + " failed")
        observed = {check["name"]: check for check in checks}
        for check_name, expected in {"returnCode": code, **REQUIRED_CHECKS[name]}.items():
            if check_name.startswith("varHandle") and device["api"] < VAR_HANDLE_API:
                continue
            check = observed.get(check_name)
            if (check is None or type(check["expected"]) is not type(expected)
                    or check["expected"] != expected):
                raise RunFailure("Scenario " + name + " is missing a required behavior observation")
        if name in ("methods-retire", "methods-retire-again"):
            callbacks = scenario.get("threadSingleStepCallbacks")
            if type(callbacks) is not int or callbacks <= 0:
                raise RunFailure("Scenario " + name + " does not prove current-thread single-step callbacks")
        # Each transaction must report its own policy outcome, not a stale one.
        if code == 0:
            reason = observed.get("lastRejection")
            if reason is None or reason["expected"] != "":
                raise RunFailure("Scenario " + name + " does not prove an empty policy rejection")
        else:
            reason = observed.get("rejectedByPolicy")
            prefix = reason and reason["expected"]
            if (not isinstance(prefix, str) or not prefix.startswith("Lcom/github/ytlog/artdcevm/fixture/")
                    or not prefix.endswith(";: ")):
                raise RunFailure("Scenario " + name + " does not prove a policy rejection")
        for check_name in UNCHANGED_CHECKS.get(name, ()):
            check = observed.get(check_name)
            if check is None or not isinstance(check["expected"], str) or not check["expected"]:
                raise RunFailure("Scenario " + name + " is missing an unchanged-definition observation")
    return report


def require_instrumentation_success(result):
    if result.returncode != 0:
        raise RunFailure("The instrumentation command returned a nonzero exit code")
    output = result.stdout + "\n" + result.stderr
    codes = re.findall(r"^INSTRUMENTATION_CODE:\s*(-?\d+)\s*$", output, re.MULTILINE)
    if codes != ["-1"] or re.search(r"^INSTRUMENTATION_(?:FAILED|ABORTED):", output, re.MULTILINE):
        raise RunFailure("Android did not report successful instrumentation completion")


def find_adb():
    executable = shutil.which("adb")
    if executable:
        return executable
    sdk = os.environ.get("ANDROID_SDK_ROOT") or os.environ.get("ANDROID_HOME")
    properties = ROOT / "local.properties"
    if not sdk and properties.exists():
        sdk = next((line.split("=", 1)[1] for line in properties.read_text().splitlines()
                    if line.startswith("sdk.dir=")), None)
    return str(Path(sdk) / "platform-tools/adb") if sdk else None


def raw_text(value):
    if isinstance(value, bytes):
        return value.decode("utf-8", errors="replace")
    return value or ""


def safe_console(text, serial):
    text = text.replace(serial, "<device>")
    text = re.sub(r"(?<![\w.])/(?:[^\s\"'<>]+)", "<local-path>", text)
    text = re.sub(r"\b(?:10(?:\.\d{1,3}){3}|192\.168(?:\.\d{1,3}){2}|"
                  r"172\.(?:1[6-9]|2\d|3[01])(?:\.\d{1,3}){2})(?::\d+)?\b", "<private-address>", text)
    return text


def safe_report(value, serial):
    if isinstance(value, str):
        return safe_console(value, serial)
    if isinstance(value, list):
        return [safe_report(item, serial) for item in value]
    if isinstance(value, dict):
        return {key: safe_report(item, serial) for key, item in value.items()}
    return value


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serial", required=True, help="ADB device selector (kept only in raw evidence)")
    parser.add_argument("--adb", default=find_adb())
    parser.add_argument("--apk", type=Path, default=ROOT / "app/build/outputs/apk/debug/app-debug.apk")
    parser.add_argument("--device-id", default="android-device-1", help="Anonymous label for the verification record")
    parser.add_argument("--timeout", type=int, default=600, help="Instrumentation timeout in seconds")
    parser.add_argument("--scenario-group", choices=("full", "ordinary", "interface-abstract"), default="full",
                        help="Run the complete suite, ordinary-class suite, or abstract interface transactions")
    parser.add_argument("--checkjni-mode", choices=("normal", "checkjni"),
                        help="Select and verify the Debug process JNI mode before engine installation")
    parser.add_argument("--evidence-root", type=Path, default=ROOT / "build/evidence/runs",
                        help="Evidence directory under ignored build/evidence")
    args = parser.parse_args(argv)
    if not args.adb:
        parser.error("Use --adb or set the SDK path in local.properties")
    if not args.apk.is_file():
        parser.error("The requested APK is missing; build :app:assembleDebug first")
    if not re.fullmatch(r"[a-z][a-z0-9-]{0,63}", args.device_id) or args.device_id == args.serial:
        parser.error("Use an anonymous --device-id such as android-device-1")
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    timestamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    if not args.evidence_root.resolve().is_relative_to((ROOT / "build/evidence").resolve()):
        parser.error("Evidence must remain under ignored build/evidence")
    out = new_evidence_directory(args.evidence_root.resolve(), timestamp)
    meta = {"deviceId": args.device_id, "apkSha256": hashlib.sha256(args.apk.read_bytes()).hexdigest(),
            "startedUtc": timestamp, "scenarioGroup": args.scenario_group, "passed": False}
    (out / "selection.json").write_text(json.dumps({"deviceId": args.device_id,
        "apk": args.apk.name}, indent=2))

    def adb(label, *command, timeout=120):
        try:
            result = subprocess.run([args.adb, "-s", args.serial, *command], capture_output=True,
                                    text=True, timeout=timeout)
        except subprocess.TimeoutExpired as error:
            (out / (label + ".txt")).write_text(safe_console(raw_text(error.stdout) + raw_text(error.stderr), args.serial))
            (out / (label + ".json")).write_text(json.dumps({"timedOut": True, "timeout": timeout}))
            raise RunFailure("The " + label + " command timed out") from error
        except OSError as error:
            (out / (label + ".txt")).write_text(safe_console(str(error), args.serial))
            raise RunFailure("The " + label + " command could not start") from error
        (out / (label + ".txt")).write_text(safe_console(result.stdout + result.stderr, args.serial))
        (out / (label + ".json")).write_text(json.dumps({"returncode": result.returncode}))
        return result

    try:
        installed = adb("install", "install", "-r", str(args.apk))
        try:
            require_install_success(installed.returncode, installed.stdout, installed.stderr)
        except RuntimeError as error:
            raise RunFailure("APK installation did not report Success") from error
        paths = adb("package-path", "shell", "pm", "path", PACKAGE)
        if paths.returncode:
            raise RunFailure("The installed APK path command failed")
        try:
            path = base_apk_path(paths.stdout)
        except RuntimeError as error:
            raise RunFailure("Android did not report exactly one installed base APK") from error
        hashed = adb("installed-hash", "shell", "sha256sum", path)
        if hashed.returncode:
            raise RunFailure("The installed APK hash command failed")
        try:
            meta["installedApkSha256"] = verified_apk_sha256(hashed.stdout, meta["apkSha256"])
        except RuntimeError as error:
            raise RunFailure("The installed APK SHA-256 does not match the requested artifact") from error
        stopped = adb("force-stop", "shell", "am", "force-stop", PACKAGE)
        if stopped.returncode:
            raise RunFailure("Android could not stop the previous application process")
        removed = adb("remove-report", "shell", "run-as", PACKAGE, "rm", "-f", REPORT_PATH)
        if removed.returncode:
            raise RunFailure("The previous device report could not be removed")
        instrumentation_error = None
        try:
            mode_args = ["-e", "checkjni", "true" if args.checkjni_mode == "checkjni" else "false"] if args.checkjni_mode else []
            if args.scenario_group != "full": mode_args += ["-e", "scenario", args.scenario_group]
            test = adb("instrumentation", "shell", "am", "instrument", "-w", "-r", *mode_args, COMPONENT,
                       timeout=args.timeout)
            meta["instrumentationExitCode"] = test.returncode
        except RunFailure as error:
            test = None
            instrumentation_error = error
            meta["instrumentationTimedOut"] = isinstance(error.__cause__, subprocess.TimeoutExpired)
        # Read the report even when Android reports failure so diagnostics are retained.
        try:
            report = adb("report-read", "exec-out", "run-as", PACKAGE, "cat", REPORT_PATH)
            try:
                saved_report = json.dumps(safe_report(json.loads(report.stdout), args.serial), indent=2)
            except (ValueError, TypeError):
                saved_report = safe_console(report.stdout, args.serial)
            (out / "structural-redefine.json").write_text(saved_report)
            meta["reportReadExitCode"] = report.returncode
        except RunFailure:
            if instrumentation_error:
                raise instrumentation_error
            raise
        if instrumentation_error:
            raise instrumentation_error
        require_instrumentation_success(test)
        if report.returncode:
            raise RunFailure("The application-private device report could not be read")
        try:
            result = json.loads(report.stdout)
        except (ValueError, TypeError) as error:
            raise RunFailure("The device report is not valid JSON") from error
        validate_report(result, scenario_group=args.scenario_group)
        meta["device"] = result["device"]
        meta["scenarioCount"] = len(result["scenarios"])
        meta["passed"] = True
        meta["status"] = "structural_redefinition_passed"
    except RunFailure as error:
        meta["status"] = "failed_or_incomplete"
        meta["error"] = str(error)
    (out / "run.json").write_text(json.dumps(meta, ensure_ascii=False, indent=2))
    print(safe_console(json.dumps(meta, ensure_ascii=False, indent=2), args.serial))
    print("Evidence: " + str(out.relative_to(ROOT)))
    return 0 if meta["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
