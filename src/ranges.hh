#ifndef ranges_hh_INCLUDED
#define ranges_hh_INCLUDED

#include <algorithm>
#include <utility>
#include <iterator>
#include <numeric>

namespace Kakoune
{

template<typename Func> struct ViewFactory { Func func; };

template<typename Func>
ViewFactory<std::decay_t<Func>>
make_view_factory(Func&& func) { return {std::forward<Func>(func)}; }

template<typename Range, typename Func>
decltype(auto) operator| (Range&& range, ViewFactory<Func> factory)
{
    return factory.func(std::forward<Range>(range));
}

template<typename Range>
struct decay_range_impl { using type = std::decay_t<Range>; };

template<typename Range>
struct decay_range_impl<Range&> { using type = Range&; };

template<typename Range>
using decay_range = typename decay_range_impl<Range>::type;

template<typename Range>
struct ReverseView
{
    decltype(auto) begin() { return m_range.rbegin(); }
    decltype(auto) end()   { return m_range.rend(); }

    Range m_range;
};

inline auto reverse()
{
    return make_view_factory([](auto&& range) {
        using Range = decltype(range);
        return ReverseView<decay_range<Range>>{std::forward<Range>(range)};
    });
}

template<typename Range>
using IteratorOf = decltype(std::begin(std::declval<Range>()));

template<typename Range>
using ValueOf = typename Range::value_type;

template<typename Range, typename Filter>
struct FilterView
{
    using RangeIt = IteratorOf<Range>;

    struct Iterator : std::iterator<std::forward_iterator_tag,
                                    typename std::iterator_traits<RangeIt>::value_type>
    {
        Iterator(const FilterView& view, RangeIt it, RangeIt end)
            : m_it{std::move(it)}, m_end{std::move(end)}, m_view{view}
        {
            do_filter();
        }

        decltype(auto) operator*() { return *m_it; }
        Iterator& operator++() { ++m_it; do_filter(); return *this; }
        Iterator operator++(int) { auto copy = *this; ++(*this); return copy; }

        friend bool operator==(const Iterator& lhs, const Iterator& rhs)
        {
            return lhs.m_it == rhs.m_it;
        }

        friend bool operator!=(const Iterator& lhs, const Iterator& rhs)
        {
            return not (lhs == rhs);
        }

        const RangeIt& base() const { return m_it; }

    private:
        void do_filter()
        {
            while (m_it != m_end and not m_view.m_filter(*m_it))
                ++m_it;
        }

        RangeIt m_it;
        RangeIt m_end;
        const FilterView& m_view;
    };

    Iterator begin() const { return {*this, std::begin(m_range), std::end(m_range)}; }
    Iterator end()   const { return {*this, std::end(m_range), std::end(m_range)}; }

    Range m_range;
    mutable Filter m_filter;
};

template<typename Filter>
inline auto filter(Filter f)
{
    return make_view_factory([f = std::move(f)](auto&& range) {
        using Range = decltype(range);
        return FilterView<decay_range<Range>, Filter>{std::forward<Range>(range), std::move(f)};
    });
}

template<typename Range, typename Transform>
struct TransformView
{
    using RangeIt = IteratorOf<Range>;
    using ResType = decltype(std::declval<Transform>()(*std::declval<RangeIt>()));

    struct Iterator : std::iterator<std::forward_iterator_tag, std::remove_reference_t<ResType>>
    {
        Iterator(const TransformView& view, RangeIt it)
            : m_it{std::move(it)}, m_view{view} {}

        decltype(auto) operator*() { return m_view.m_transform(*m_it); }
        Iterator& operator++() { ++m_it; return *this; }
        Iterator operator++(int) { auto copy = *this; ++m_it; return copy; }

        friend bool operator==(const Iterator& lhs, const Iterator& rhs)
        {
            return lhs.m_it == rhs.m_it;
        }

        friend bool operator!=(const Iterator& lhs, const Iterator& rhs)
        {
            return not (lhs == rhs);
        }

        RangeIt base() const { return m_it; }

    private:
        RangeIt m_it;
        const TransformView& m_view;
    };

    Iterator begin() const { return {*this, std::begin(m_range)}; }
    Iterator end()   const { return {*this, std::end(m_range)}; }

    Range m_range;
    mutable Transform m_transform;
};

template<typename Transform>
inline auto transform(Transform t)
{
    return make_view_factory([t = std::move(t)](auto&& range) {
        using Range = decltype(range);
        return TransformView<decay_range<Range>, Transform>{std::forward<Range>(range), std::move(t)};
    });
}

template<typename Range, typename Separator = ValueOf<Range>,
         typename ValueTypeParam = void>
struct SplitView
{
    using RangeIt = IteratorOf<Range>;
    using ValueType = std::conditional_t<std::is_same<void, ValueTypeParam>::value,
                                         std::pair<IteratorOf<Range>, IteratorOf<Range>>,
                                         ValueTypeParam>;

    struct Iterator : std::iterator<std::forward_iterator_tag, ValueType>
    {
        Iterator(RangeIt pos, RangeIt end, char separator)
         : pos(pos), sep(pos), end(end), separator(separator)
        {
            while (sep != end and *sep != separator)
                ++sep;
        }

        Iterator& operator++() { advance(); return *this; }
        Iterator operator++(int) { auto copy = *this; advance(); return copy; }

        bool operator==(const Iterator& other) const { return pos == other.pos; }
        bool operator!=(const Iterator& other) const { return pos != other.pos; }

        ValueType operator*() { return {pos, sep}; }

    private:
        void advance()
        {
            if (sep == end)
            {
                pos = end;
                return;
            }

            pos = sep+1;
            for (sep = pos; sep != end; ++sep)
            {
                if (*sep == separator)
                    break;
            }
        }

        RangeIt pos;
        RangeIt sep;
        RangeIt end;
        Separator separator;
    };

    Iterator begin() const { return {std::begin(m_range), std::end(m_range), m_separator}; }
    Iterator end()   const { return {std::end(m_range), std::end(m_range), m_separator}; }

    Range m_range;
    Separator m_separator;
};

template<typename ValueType = void, typename Separator>
auto split(Separator separator)
{
    return make_view_factory([s = std::move(separator)](auto&& range) {
        using Range = decltype(range);
        return SplitView<decay_range<Range>, Separator, ValueType>{std::forward<Range>(range), std::move(s)};
    });
}

template<typename Range1, typename Range2>
struct ConcatView
{
    using RangeIt1 = decltype(begin(std::declval<Range1>()));
    using RangeIt2 = decltype(begin(std::declval<Range2>()));
    using ValueType = typename std::common_type_t<typename std::iterator_traits<RangeIt1>::value_type,
                                                  typename std::iterator_traits<RangeIt2>::value_type>;

    struct Iterator : std::iterator<std::forward_iterator_tag, ValueType>
    {
        static_assert(std::is_convertible<typename std::iterator_traits<RangeIt1>::value_type, ValueType>::value, "");
        static_assert(std::is_convertible<typename std::iterator_traits<RangeIt2>::value_type, ValueType>::value, "");

        Iterator(RangeIt1 it1, RangeIt1 end1, RangeIt2 it2)
            : m_it1(std::move(it1)), m_end1(std::move(end1)),
              m_it2(std::move(it2)) {}

        ValueType operator*() { return is2() ? *m_it2 : *m_it1; }
        Iterator& operator++() { if (is2()) ++m_it2; else ++m_it1; return *this; }
        Iterator operator++(int) { auto copy = *this; ++*this; return copy; }

        friend bool operator==(const Iterator& lhs, const Iterator& rhs)
        {
            return lhs.m_it1 == rhs.m_it1 and lhs.m_end1 == rhs.m_end1 and
                   lhs.m_it2 == rhs.m_it2;
        }

        friend bool operator!=(const Iterator& lhs, const Iterator& rhs)
        {
            return not (lhs == rhs);
        }

    private:
        bool is2() const { return m_it1 == m_end1; }

        RangeIt1 m_it1;
        RangeIt1 m_end1;
        RangeIt2 m_it2;
    };

    ConcatView(Range1& range1, Range2& range2)
        : m_range1(range1), m_range2(range2) {}

    Iterator begin() const { return {m_range1.begin(), m_range1.end(), m_range2.begin()}; }
    Iterator end()   const { return {m_range1.end(), m_range1.end(), m_range2.end()}; }

private:
    Range1& m_range1;
    Range2& m_range2;
};

template<typename Range1, typename Range2>
ConcatView<Range1, Range2> concatenated(Range1&& range1, Range2&& range2)
{
    return {range1, range2};
}

template<typename Range, typename T>
auto find(Range&& range, const T& value)
{
    using std::begin; using std::end;
    return std::find(begin(range), end(range), value);
}

template<typename Range, typename T>
auto find_if(Range&& range, T op)
{
    using std::begin; using std::end;
    return std::find_if(begin(range), end(range), op);
}

template<typename Range, typename T>
bool contains(Range&& range, const T& value)
{
    using std::end;
    return find(range, value) != end(range);
}

template<typename Range, typename T>
bool contains_that(Range&& range, T op)
{
    using std::end;
    return find_if(range, op) != end(range);
}

template<typename Range, typename U>
void unordered_erase(Range&& vec, U&& value)
{
    auto it = find(vec, std::forward<U>(value));
    if (it != vec.end())
    {
        using std::swap;
        swap(vec.back(), *it);
        vec.pop_back();
    }
}

template<typename Range, typename Init, typename BinOp>
Init accumulate(Range&& c, Init&& init, BinOp&& op)
{
    using std::begin; using std::end;
    return std::accumulate(begin(c), end(c), init, op);
}

template<typename Container>
auto gather()
{
    return make_view_factory([](auto&& range) {
        using std::begin; using std::end;
        return Container(begin(range), end(range));
    });
}

}

#endif // ranges_hh_INCLUDED
