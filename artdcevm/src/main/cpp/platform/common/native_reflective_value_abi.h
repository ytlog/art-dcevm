#pragma once
// Fixed ART reflective_value_visitor.h (11--17): exact virtual source ABI.
namespace art {
class ArtMethod;class ArtField;class ReflectionSourceInfo;
class ReflectiveValueVisitor {
 public:
  virtual ~ReflectiveValueVisitor()=default;
  virtual ArtMethod* VisitMethod(ArtMethod*,const ReflectionSourceInfo&)=0;
  virtual ArtField* VisitField(ArtField*,const ReflectionSourceInfo&)=0;
};
}
