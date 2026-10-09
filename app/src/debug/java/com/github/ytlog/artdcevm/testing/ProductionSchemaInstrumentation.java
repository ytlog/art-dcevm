package com.github.ytlog.artdcevm.testing;

import android.app.Instrumentation;
import android.os.Bundle;

/** Runs one shared required production scenario using only the installed lab APK. */
public final class ProductionSchemaInstrumentation extends Instrumentation {
    private Bundle arguments;
    @Override public void onCreate(Bundle input) {
        super.onCreate(input);arguments=input==null?new Bundle():new Bundle(input);start();
    }
    @Override public void onStart() {
        Bundle output=new Bundle();
        boolean passed=false;
        try {
            new ProductionSchemaScenarios(this,arguments).run(arguments.getString("productionSchemaCase"));
            passed=true;
        } catch(Throwable error) {
            output.putString("schemaRunnerFailure",error.toString());
        }
        output.putString("schemaRunnerCompleted",passed?"true":"false");
        finish(passed?-1:0,output);
    }
}
