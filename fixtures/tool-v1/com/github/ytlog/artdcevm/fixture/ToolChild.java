package com.github.ytlog.artdcevm.fixture;

/** Added by the tool; extends the class the same update redefines. */
public class ToolChild extends ToolHost {
    public int child = 3;
    @Override public int describe() {
        extra = 4;
        return extra * 10 + child;
    }
}
