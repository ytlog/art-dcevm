# Maven 发布

库模块应用 [gradle/publish-maven.gradle](../gradle/publish-maven.gradle)，使用 Vanniktech Maven Publish 0.37.0 发布 release AAR、Java sources、Javadoc、POM 和 Gradle Module Metadata。依赖从 Android release component 生成，不手写依赖列表，因此 arm64 包的 ShadowHook 会传递到使用方。

## 坐标与构建

默认坐标为 `com.github.ytlog:artdcevm:0.1.0-SNAPSHOT`。通过 `-PpublishGroupId`、`-PpublishArtifactId`、`-PpublishVersion` 覆盖；正式发布需使用已验证归属的 groupId 和明确版本。仅支持 arm64-v8a。仅发布 release，禁止发布 `forceCheckedHandles` 测试构建。

## 本地验证

```sh
# 发布到工程内已忽略的文件仓库，无需凭据或签名
./gradlew :artdcevm:publishAllPublicationsToTargetRepository \
  -PpublishRepositoryUrl="file://$PWD/build/evidence/maven-repository" \
  -PpublishVersion=0.1.0-local

# 或发布到 ~/.m2/repository
./gradlew :artdcevm:publishToMavenLocal -PpublishVersion=0.1.0-local
```

本地消费时添加相应 `maven { url = uri(...) }` 或 `mavenLocal()`，依赖声明为 `implementation 'com.github.ytlog:artdcevm:0.1.0-local'`。仍需 `mavenCentral()` 解析 ShadowHook。只复制 AAR 文件会丢失传递依赖元数据，验证时应通过 Maven 坐标解析。

## 普通 Maven 仓库

在用户级 `~/.gradle/gradle.properties` 或 CI 环境中配置凭据；不要写入仓库：

```properties
publishRepositoryUrl=https://YOUR_MAVEN_HOST/repository/releases/
publishRepositoryUsername=YOUR_USERNAME
publishRepositoryPassword=YOUR_TOKEN
```

```sh
./gradlew :artdcevm:publishAllPublicationsToTargetRepository \
  -PpublishVersion=0.1.0
```

环境变量可使用 `ORG_GRADLE_PROJECT_publishRepositoryUsername` 和 `ORG_GRADLE_PROJECT_publishRepositoryPassword`。私服消费者需在 `settings.gradle` 添加对应仓库地址。

## Maven Central

Central Portal 发布需要账号、已验证的命名空间、GPG 签名及项目许可证信息。仓库当前未确定项目许可证；发布前先确定并加入许可证文件，再配置对应名称和 URL，不能直接沿用第三方库许可证。

用户级 `~/.gradle/gradle.properties` 中配置：

```properties
# 使用 Central Portal 生成的用户 token，非网页登录密码
mavenCentralUsername=YOUR_TOKEN_USERNAME
mavenCentralPassword=YOUR_TOKEN_PASSWORD
publishLicenseName=YOUR_PROJECT_LICENSE_NAME
publishLicenseUrl=https://YOUR_PROJECT_LICENSE_URL

signing.keyId=YOUR_GPG_KEY_ID
signing.password=YOUR_GPG_PASSWORD
signing.secretKeyRingFile=/ABSOLUTE/PATH/TO/secring.gpg
```

CI 可通过 `ORG_GRADLE_PROJECT_signingInMemoryKey` 提供 ASCII armored 私钥、`ORG_GRADLE_PROJECT_signingInMemoryKeyPassword` 提供密码，Central token 和许可证字段同样支持 `ORG_GRADLE_PROJECT_` 前缀。

```sh
# 上传并验证，由发布者在 Central Portal 中点击 Publish
./gradlew :artdcevm:publishToMavenCentral \
  -PenableMavenCentral=true -PpublishVersion=0.1.0

# 或上传、验证并自动正式发布
./gradlew :artdcevm:publishAndReleaseToMavenCentral \
  -PenableMavenCentral=true -PpublishVersion=0.1.0
```

`enableMavenCentral` 默认关闭，本地和普通仓库验证无需 Central 凭据。上述正式发布命令仅供发布者执行；脚本加入仓库不表示产物已上线。默认 `-SNAPSHOT` 版本发布至 Central 的 snapshot 仓库，不能用 `mavenCentral()` 获取；README 的接入方式面向正式版本。

配置依据：[Android 发布组件与依赖元数据](https://developer.android.com/build/publish-library/configure-pub-variants)、[发布插件 Central Portal 指南](https://vanniktech.github.io/gradle-maven-publish-plugin/central/)。
