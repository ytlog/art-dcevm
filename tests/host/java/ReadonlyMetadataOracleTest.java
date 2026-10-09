import com.github.ytlog.artdcevm.testing.MethodDexOracle;
import java.lang.reflect.Executable;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashSet;
public final class ReadonlyMetadataOracleTest {
 public static void main(String[] args)throws Exception {
  ArrayList<Executable> all=new ArrayList<>();Collections.addAll(all,ReadonlyMetadataFixture.class.getDeclaredConstructors());
  Collections.addAll(all,ReadonlyMetadataFixture.class.getDeclaredMethods());
  MethodDexOracle.Result result=MethodDexOracle.read(args[0],all.toArray(new Executable[0]));
  if(result.indices.length!=6||result.directMethods!=4||result.virtualMethods!=2||result.fields.length!=3||result.staticFields!=1||result.instanceFields!=2||result.classDefIndex<0||result.typeIndex<0)throw new AssertionError("Complete class_data oracle missing");
  HashSet<Integer> fieldIds=new HashSet<>();for(int id:result.fieldIndices)fieldIds.add(id);
  if(fieldIds.size()!=3)throw new AssertionError("Distinct field_ids oracle missing");
  if(!Integer.valueOf(42).equals(ReadonlyMetadataFixture.class.getDeclaredMethod("value",int.class).invoke(new ReadonlyMetadataFixture(),1)))throw new AssertionError("Fixture changed");
  System.out.println("Real D8 Method/Class/Field oracle: six methods, three distinct fields, complete direct/static counts passed");
 }
}
