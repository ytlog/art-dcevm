# Maven 发布

库模块应用 [gradle/publish-maven.gradle](../gradle/publish-maven.gradle)，使用 Vanniktech Maven Publish 0.37.0 发布 release AAR、sources、Javadoc、POM 和 Gradle Module Metadata。依赖从 Android release component 生成，不手写依赖列表：ShadowHook 作为运行时依赖传递给使用方，使用方无需单独声明。

## 坐标与许可证

- 默认坐标为 `io.github.ytlog:artdcevm:0.1.0-alpha`，可用 `-PpublishGroupId`、`-PpublishArtifactId`、`-PpublishVersion` 覆盖。
- `io.github.ytlog` 是 Central Portal 为 GitHub 账号 `ytlog` 自动开通的命名空间；`com.github.*` 需要证明拥有 github.com，无法注册。groupId 只是 Maven 坐标，Java 包名仍为 `com.github.ytlog.artdcevm`。
- 许可证为 MIT（根目录 [LICENSE](../LICENSE)），已固定写入 POM。
- 只发布 arm64-v8a 的 release 构建。
- 项目没有 Kotlin 源码，[gradle.properties](../gradle.properties) 中的 `kotlin.stdlib.default.dependency=false` 阻止 AGP 内置 Kotlin 把 `kotlin-stdlib` 写进 POM。

Maven Central 上的版本不可覆盖或删除，同一版本号只能发布一次；修正问题需发布新版本（如 `0.1.0-alpha2`）。

## 本地验证

发布前先发布到本地仓库，检查 POM 与产物：

```sh
# 发布到工程内已忽略的文件仓库，无需凭据或签名
./gradlew :artdcevm:publishAllPublicationsToTargetRepository \
  -PpublishRepositoryUrl="file://$PWD/build/evidence/maven-repository"

# 或发布到 ~/.m2/repository
./gradlew :artdcevm:publishToMavenLocal
```

产物位于 `io/github/ytlog/artdcevm/0.1.0-alpha/`：`.aar`、`.pom`、`.module`、`-sources.jar`、`-javadoc.jar`。POM 的依赖应只有 ShadowHook（`runtime`）。本地消费时添加对应的 `maven { url = uri(...) }` 或 `mavenLocal()`，仍需 `mavenCentral()` 解析 ShadowHook。只复制 AAR 文件会丢失传递依赖。

## 发布到 Maven Central

### 一次性准备

1. 用 GitHub 账号 `ytlog` 登录 [Central Portal](https://central.sonatype.com/)，在 Namespaces 页确认 `io.github.ytlog` 已验证。
2. 在 Account 页生成 User Token（发布用的用户名和密码，不是网页登录密码）。
3. 生成 GPG 密钥，并把公钥上传到 Central 支持的密钥服务器：

   ```sh
   gpg --full-generate-key
   gpg --list-secret-keys --keyid-format short    # 记下 sec 行 / 后的 8 位 KEY_ID
   gpg --keyserver keyserver.ubuntu.com --send-keys <KEY_ID>
   gpg --export-secret-keys -o ~/.gnupg/secring.gpg <KEY_ID>
   ```

4. 在用户级 `~/.gradle/gradle.properties` 中配置（不要写入仓库）：

   ```properties
   mavenCentralUsername=<User Token 用户名>
   mavenCentralPassword=<User Token 密码>

   signing.keyId=<KEY_ID>
   signing.password=<GPG 密钥密码>
   signing.secretKeyRingFile=<secring.gpg 的绝对路径>
   ```

   CI 中可改用环境变量：`ORG_GRADLE_PROJECT_mavenCentralUsername`、`ORG_GRADLE_PROJECT_mavenCentralPassword`，以及 `ORG_GRADLE_PROJECT_signingInMemoryKey`（ASCII armored 私钥）和 `ORG_GRADLE_PROJECT_signingInMemoryKeyPassword`。

### 发布

```sh
# 上传并校验，再到 Central Portal 的 Deployments 页检查后点击 Publish
./gradlew :artdcevm:publishToMavenCentral -PenableMavenCentral=true

# 或上传、校验后自动正式发布
./gradlew :artdcevm:publishAndReleaseToMavenCentral -PenableMavenCentral=true
```

`enableMavenCentral` 默认关闭，本地验证无需 Central 凭据。正式发布后通常十几分钟到半小时可经 `mavenCentral()` 解析。

## 其他 Maven 仓库

发布到 Nexus、Artifactory 或 GitHub Packages 时，在用户级 `~/.gradle/gradle.properties` 或 CI 环境中配置：

```properties
publishRepositoryUrl=https://YOUR_MAVEN_HOST/repository/releases/
publishRepositoryUsername=YOUR_USERNAME
publishRepositoryPassword=YOUR_TOKEN
```

```sh
./gradlew :artdcevm:publishAllPublicationsToTargetRepository
```

使用方需在 `settings.gradle` 中添加对应仓库地址。

配置依据：[Android 发布组件与依赖元数据](https://developer.android.com/build/publish-library/configure-pub-variants)、[发布插件 Central Portal 指南](https://vanniktech.github.io/gradle-maven-publish-plugin/central/)、[Central 命名空间注册](https://central.sonatype.org/register/namespace/)。
