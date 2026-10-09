public final class ReadonlyMetadataFixture {
 public int instanceField;private long privateField;public static final int staticField=37;
 public ReadonlyMetadataFixture(){} public ReadonlyMetadataFixture(int ignored){}
 public int value(int input){return input+41;}
 public static int staticValue(int input){return input+17;}
 private int privateValue(int input){return input+13;}
 public native int nativeValue(int input);
}
