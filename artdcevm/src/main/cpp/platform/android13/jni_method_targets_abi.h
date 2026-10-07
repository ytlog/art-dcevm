#pragma once

namespace artdcevm::android13 {
// IsCopied is read through the locked ART declaration by the temporary bridge.
using JniMethodTarget = void* (*)(void* method, bool copied);
}
