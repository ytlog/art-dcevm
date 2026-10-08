#pragma once
#include "platform/common/native_source_commit.h"
#include "platform/common/modern_method_services.h"
namespace artdcevm::modern_source_commit {
using Dependencies=native_source_commit::Dependencies;
using Services=native_source_commit::Services;
// Fixed modern ti_redefine.cc restores old non-copied method and all field
// declaring roots after heap replacement. The original native owner supplies
// each already-prepared actual method table; no JNI is allowed here.
std::unique_ptr<Services> Prepare(HookEngine&,const Dependencies&,
    const source_commit::Plan&,const std::vector<const modern_methods::Service*>&,
    std::string* error);
}
