#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <optional>
namespace lumashot::clipboard {
enum class CodeLanguage { Auto, Plain, Cpp, Python, JavaScript, TypeScript, Json, Bash, PowerShell, Sql, CSharp, Markdown };
enum class CodeColor { Text, Keyword, String, Comment, Number, Type, Operator, Preprocessor };
struct CodeSpan { uint32_t start{},length{};CodeColor color{}; };
struct HighlightResult { uint64_t generation{};CodeLanguage language{CodeLanguage::Plain};std::vector<CodeSpan> spans; };
const wchar_t* LanguageName(CodeLanguage language);
HighlightResult HighlightCode(std::wstring_view text,CodeLanguage language=CodeLanguage::Auto);
class HighlightWorker {
public:
    static constexpr UINT Ready=WM_APP+0x4c1;
    explicit HighlightWorker(HWND window);
    ~HighlightWorker();
    void Request(std::wstring text,CodeLanguage language,uint64_t generation);
    std::optional<HighlightResult> Take();
private:
    struct Job {std::wstring text;CodeLanguage language;uint64_t generation;};
    HWND window_;std::mutex mutex_;std::condition_variable condition_;bool stopping_{};
    std::optional<Job> pending_;std::optional<HighlightResult> result_;uint64_t latest_{};std::thread thread_;
};
}
