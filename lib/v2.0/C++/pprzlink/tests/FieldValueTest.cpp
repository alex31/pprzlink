#include <pprzlink/FieldValue.h>
#include <deque>
#include <forward_list>
#include <iostream>
#include <list>
#include <set>
#include <span>
#include <string_view>

using pprzlink::FieldValue;
using pprzlink::MessageField;

namespace {
  template<class T>
  concept CanReadField = requires(const FieldValue &field, T &output) {
    field.getValue(output);
  };

  template<class T>
  concept CanReturnField = requires(const FieldValue &field) {
    { field.getValue<T>() } -> std::same_as<T>;
  };

  struct ValueTypeOnly { using value_type = int; };
  struct OutputBuffer {
    using value_type = int16_t;
    std::vector<int16_t> values;
    void clear() { values.clear(); }
    void push_back(int16_t value) { values.push_back(value); }
  };

  // Check overload selection, including types previously rejected in function bodies.
  static_assert(std::constructible_from<FieldValue, const MessageField&, int>);
  static_assert(std::constructible_from<FieldValue, const MessageField&, double>);
  static_assert(std::constructible_from<FieldValue, const MessageField&, char>);
  static_assert(std::constructible_from<FieldValue, const MessageField&, bool>);
  static_assert(std::constructible_from<FieldValue, const MessageField&, std::string>);
  static_assert(std::constructible_from<FieldValue, const MessageField&, const char*>);
  static_assert(std::constructible_from<FieldValue, const MessageField&, const int*, size_t>);
  static_assert(std::constructible_from<FieldValue, const MessageField&, std::vector<int>>);
  static_assert(std::constructible_from<FieldValue, const MessageField&, std::array<int, 3>>);
  static_assert(std::constructible_from<FieldValue, const MessageField&, std::span<const int>>);
  static_assert(std::constructible_from<FieldValue, const MessageField&, std::set<int>>);
  static_assert(!std::constructible_from<FieldValue, const MessageField&, int*>);
  static_assert(!std::constructible_from<FieldValue, const MessageField&, ValueTypeOnly>);
  static_assert(!std::constructible_from<FieldValue, const MessageField&, std::forward_list<int>>);
  static_assert(CanReadField<int> && CanReadField<double> && CanReadField<char>);
  static_assert(CanReadField<std::string> && CanReadField<std::vector<int>>);
  static_assert(CanReadField<std::array<int, 3>> && CanReadField<std::list<int>>);
  static_assert(CanReadField<std::deque<int>>);
  static_assert(CanReadField<OutputBuffer>);
  static_assert(!CanReadField<ValueTypeOnly> && !CanReadField<std::set<int>>);
  static_assert(!CanReadField<std::span<int>> && !CanReadField<std::string_view>);

  static_assert(CanReturnField<int> && CanReturnField<std::string>);
  static_assert(CanReturnField<std::array<int, 3>> && CanReturnField<OutputBuffer>);
  static_assert(!CanReturnField<ValueTypeOnly> && !CanReturnField<std::set<int>>);
  static_assert(!CanReturnField<std::span<int>> && !CanReturnField<std::string_view>);

  void require(bool condition, const char *description)
  {
    if (!condition) throw std::runtime_error(description);
  }

  template<class Exception, class Callback>
  void expectException(Callback callback)
  {
    try {
      callback();
    } catch (const Exception&) {
      return;
    }
    throw std::runtime_error("Expected exception was not thrown");
  }

  template<class T>
  T read(const FieldValue &field)
  {
    T result{};
    field.getValue(result);
    return result;
  }
}

int main()
{
  try {
    const MessageField integer("number", "int16");
    const MessageField real("real", "double");
    const MessageField text("text", "string");
    const MessageField chars("chars", "char[]");
    const MessageField numbers("numbers", "int16[]");
    const MessageField fixed("fixed", "int16[3]");

    require(read<int16_t>(FieldValue(integer, -123)) == -123, "Integer input");
    require(read<double>(FieldValue(real, 1.25f)) == 1.25, "Floating-point input");
    require(read<uint8_t>(FieldValue(MessageField("flag", "uint8"), true)) == 1, "Bool input");
    require(read<char>(FieldValue(MessageField("char", "char"), 'x')) == 'x', "Char input");
    const FieldValue number(integer, 123);
    expectException<std::bad_variant_access>([&] { (void)read<double>(number); });

    const std::string words = "two words";
    require(read<std::string>(FieldValue(text, words)) == words, "std::string input");
    require(read<std::string>(FieldValue(text, "literal")) == "literal", "String literal input");
    char mutableText[] = "buffer";
    require(read<std::string>(FieldValue(text, mutableText)) == "buffer", "Mutable C string input");
    const FieldValue empty(text, std::string{});
    std::string explicitResult;
    empty.getValue<std::string>(explicitResult);
    require(explicitResult.empty(), "Explicit string getter template");
    require(read<std::vector<char>>(FieldValue(chars, "abc")) == std::vector<char>({'a', 'b', 'c'}),
            "C string to char array");
    require(read<std::vector<char>>(FieldValue(chars, std::string_view("abc"))) ==
            std::vector<char>({'a', 'b', 'c'}), "Character view keeps array semantics");
    expectException<std::logic_error>([&] { (void)FieldValue(integer, words); });

    const int raw[] = {1, -2, 3};
    const std::vector<int16_t> expected = {1, -2, 3};
    require(read<std::vector<int16_t>>(FieldValue(numbers, raw, 3)) == expected, "Pointer and length input");
    require(read<std::vector<int16_t>>(FieldValue(numbers, std::span(raw))) == expected, "Span input");
    require(read<std::vector<int16_t>>(FieldValue(numbers, std::list<int>{1, -2, 3})) == expected, "List input");
    const FieldValue sequence(numbers, std::vector<int>{1, -2, 3});
    std::vector<int16_t> replaced{99};
    sequence.getValue(replaced);
    require(replaced == expected, "Vector output replaces its previous contents");
    require(sequence.getValue<std::vector<int16_t>>() == expected, "Return vector value");
    require(sequence.getValue<std::array<int16_t, 3>>() == std::array<int16_t, 3>{1, -2, 3},
            "Return fixed array value");
    require(number.getValue<int16_t>() == 123, "Return scalar value");
    require(read<OutputBuffer>(sequence).values == expected, "Output requires no iteration or size");
    require(read<std::list<int16_t>>(sequence) == std::list<int16_t>({1, -2, 3}), "List output");
    require(read<std::deque<int16_t>>(sequence) == std::deque<int16_t>({1, -2, 3}), "Deque output");
    require(read<std::array<int16_t, 3>>(sequence) == std::array<int16_t, 3>{1, -2, 3}, "Fixed array output");
    require(read<std::vector<int16_t>>(FieldValue(fixed, std::array<int, 3>{1, -2, 3})) == expected,
            "Fixed array input");
    require(read<std::vector<int16_t>>(FieldValue(numbers, std::set<int>{1, 2})) == std::vector<int16_t>({1, 2}),
            "Read-only container input");
    require(read<std::vector<int16_t>>(FieldValue(numbers, std::vector<int>{})).empty(), "Empty array");
    require(read<std::vector<uint8_t>>(FieldValue(MessageField("flags", "uint8[]"), std::vector<bool>{true, false})) ==
            std::vector<uint8_t>({1, 0}), "Proxy container input");
    expectException<std::logic_error>([&] { (void)FieldValue(fixed, std::vector<int>{1}); });
    expectException<std::logic_error>([&] { (void)FieldValue(integer, raw, 3); });
    std::cout << "FieldValue overloads, conversions and error behavior passed\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
