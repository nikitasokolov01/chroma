# Contributing to Chroma

Chroma is an independent Prism Launcher UI fork. Report bugs, propose changes, and open pull requests in [nikitasokolov01/chroma](https://github.com/nikitasokolov01/chroma). The current release is **v0.1.0 preview**, with Windows x64 packages.

Start with the [build and test guide](docs/CHROMA.md#build-from-source). For larger changes, open a Chroma issue first so the intended behavior and scope can be discussed. Keep changes focused and preserve Prism's instance, account, installation, and settings behavior when updating the interface.

## Before opening a pull request

- Explain the user-facing problem and the resulting behavior. Link related Chroma issues.
- Build the changed code and run the relevant tests. Include the commands and results in the pull request.
- For interface changes, check a normal window and the 680×640 compact layout. Include screenshots; use synthetic profiles rather than personal account data.
- Keep existing license and attribution notices. Third-party components retain their own licenses; see [LICENSE](LICENSE) and [COPYING.md](COPYING.md).
- Sign off every commit as described below.

There is no active GitHub Actions validation workflow for this preview. Local test results are part of the review; do not assume that opening a pull request runs the build automatically.

## Code style

All files are formatted with `clang-format` using the configuration in `.clang-format`. Ensure it is run on changed files before committing!

Please also follow the project's conventions for C++:

- Class and type names should be formatted as `PascalCase`: `MyClass`.
- Private or protected class data members should be formatted as `camelCase` prefixed with `m_`: `m_myCounter`.
- Private or protected `static` class data members should be formatted as `camelCase` prefixed with `s_`: `s_instance`.
- Public class data members should be formatted as `camelCase` without the prefix: `dateOfBirth`.
- Public, private or protected `static const` class data members should be formatted as `SCREAMING_SNAKE_CASE`: `MAX_VALUE`.
- Class function members should be formatted as `camelCase` without a prefix: `incrementCounter`.
- Global functions and non-`const` global variables should be formatted as `camelCase` without a prefix: `globalData`.
- `const` global variables and macros should be formatted as `SCREAMING_SNAKE_CASE`: `LIGHT_GRAY`.
- enum constants should be formatted as `PascalCase`: `CamelusBactrianus`
- Avoid inventing acronyms or abbreviations especially for a name of multiple words - like `tp` for `texturePack`.
- Avoid using `[[nodiscard]]` unless ignoring the return value is likely to cause a bug in cases such as:
  - A function allocates memory or another resource and the caller needs to clean it up.
  - A function has side effects and an error status is returned.
  - A function is likely be mistaken for having side effects.
- A plain getter is unlikely to cause confusion and adding `[[nodiscard]]` can create clutter and inconsistency.

Most of these rules are included in the `.clang-tidy` file, so you can run `clang-tidy` to check for any violations.

Here is what these conventions with the formatting configuration look like:

```c++
#define AWESOMENESS 10

constexpr double PI = 3.14159;

enum class PizzaToppings { HamAndPineapple, OreoAndKetchup };

struct Person {
    QString name;
    QDateTime dateOfBirth;

    long daysOld() const { return dateOfBirth.daysTo(QDateTime::currentDateTime()); }
};

class ImportantClass {
   public:
    void incrementCounter()
    {
        if (m_counter + 1 > MAX_COUNTER_VALUE)
            throw std::runtime_error("Counter has reached limit!");

        ++m_counter;
    }

    int counter() const { return m_counter; }

   private:
    static constexpr int MAX_COUNTER_VALUE = 100;
    int m_counter;
};

ImportantClass importantClassInstance;
```

If you see any names which do not follow these conventions, it is preferred that you leave them be - renames increase the number of changes therefore make reviewing harder and make your PR more prone to conflicts. However, if you're refactoring a whole class anyway, it's fine.

## Signing your work

In an effort to ensure that the code you contribute is actually compatible with the licenses in this codebase, we require you to sign-off all your contributions.

This can be done by appending `-s` to your `git commit` call, or by manually appending the following text to your commit message:

```text
<commit message>

Signed-off-by: Author name <Author email>
```

By signing off your work, you agree to the terms below:

```text
Developer's Certificate of Origin 1.1

By making a contribution to this project, I certify that:

(a) The contribution was created in whole or in part by me and I
    have the right to submit it under the open source license
    indicated in the file; or

(b) The contribution is based upon previous work that, to the best
    of my knowledge, is covered under an appropriate open source
    license and I have the right under that license to submit that
    work with modifications, whether created in whole or in part
    by me, under the same open source license (unless I am
    permitted to submit under a different license), as indicated
    in the file; or

(c) The contribution was provided directly to me by some other
    person who certified (a), (b) or (c) and I have not modified
    it.

(d) I understand and agree that this project and the contribution
    are public and that a record of the contribution (including all
    personal information I submit with it, including my sign-off) is
    maintained indefinitely and may be redistributed consistent with
    this project or the open source license(s) involved.
```

Every contributed commit must carry the sign-off. Maintainers review sign-offs as part of the pull request; this preview does not promise an automated DCO check.

As a bonus, you can also [cryptographically sign your commits][gh-signing-commits] and enable [vigilant mode][gh-vigilant-mode] on GitHub.

[gh-signing-commits]: https://docs.github.com/en/authentication/managing-commit-signature-verification/signing-commits
[gh-vigilant-mode]: https://docs.github.com/en/authentication/managing-commit-signature-verification/displaying-verification-statuses-for-all-of-your-commits

## Release changes

Target the repository's default branch unless a maintainer requests another branch. Mention any migration, packaging, or external service configuration changes in the pull request. Chroma currently publishes preview releases manually; there is no automated backport workflow.
