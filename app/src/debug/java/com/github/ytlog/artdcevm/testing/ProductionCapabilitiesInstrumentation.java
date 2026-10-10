package com.github.ytlog.artdcevm.testing;

import android.app.Instrumentation;
import android.os.Bundle;

/** Runs the complete shared bootstrap/member suite with only the lab APK. */
public final class ProductionCapabilitiesInstrumentation extends Instrumentation {
    private Bundle arguments;
    @Override public void onCreate(Bundle input) {
        super.onCreate(input);arguments=input==null?new Bundle():new Bundle(input);start();
    }
    @Override public void onStart() {
        Bundle output=new Bundle();boolean passed=false;
        try {
            new ProductionCapabilitiesScenarios(this,arguments).run();passed=true;
        } catch(Throwable error) {
            output.putString("bootstrapRunnerFailure",error.toString());
        }
        output.putString("bootstrapRunnerCompleted",passed?"true":"false");
        finish(passed?-1:0,output);
    }
}
