#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

namespace tcr {
namespace py {
namespace sort_detail {

constexpr ptrdiff_t kMinGallop = 7;

template <class T, class Less>
class Timsort {
 public:
  Timsort(std::vector<T>& list, Less& lt) : list_(list), lt_(lt) {}

  void run() {
    ptrdiff_t n = static_cast<ptrdiff_t>(list_.size());
    if (n < 2) return;
    listlen_ = n;
    ptrdiff_t minrun = compute_minrun(n);
    ptrdiff_t lo = 0, remaining = n;
    do {
      ptrdiff_t r = count_run(list_.data() + lo, remaining);
      if (r < minrun) {
        ptrdiff_t force = remaining <= minrun ? remaining : minrun;
        binarysort(list_.data() + lo, force, r);
        r = force;
      }
      found_new_run(r);
      pending_.push_back(Run{lo, r, 0});
      lo += r;
      remaining -= r;
    } while (remaining);
    force_collapse();
  }

 private:
  struct Run {
    ptrdiff_t base;
    ptrdiff_t len;
    int power;
  };

  bool lt(const T& a, const T& b) { return lt_(a, b); }

  static ptrdiff_t compute_minrun(ptrdiff_t n) {
    ptrdiff_t r = 0;
    while (n >= 64) {
      r |= n & 1;
      n >>= 1;
    }
    return n + r;
  }

  ptrdiff_t count_run(T* lo, ptrdiff_t remaining) {
    ptrdiff_t n;
    for (n = 1; n < remaining; ++n) {
      if (lt(lo[n], lo[n - 1])) break;
    }
    if (n == remaining) return n;
    if (n > 1) {
      if (lt(lo[0], lo[n - 1])) return n;
      std::reverse(lo, lo + n);
    }
    ++n;
    ptrdiff_t neq = 0;
    auto reverse_last_neq = [&]() {
      if (neq) {
        ++neq;
        std::reverse(lo + (n - neq), lo + n);
        neq = 0;
      }
    };
    for (; n < remaining; ++n) {
      if (lt(lo[n], lo[n - 1])) {
        reverse_last_neq();
      } else if (lt(lo[n - 1], lo[n])) {
        break;
      } else {
        ++neq;
      }
    }
    reverse_last_neq();
    std::reverse(lo, lo + n);
    for (; n < remaining; ++n) {
      if (lt(lo[n], lo[n - 1])) break;
    }
    return n;
  }

  void binarysort(T* lo, ptrdiff_t n, ptrdiff_t ok) {
    if (!ok) ++ok;
    for (; ok < n; ++ok) {
      ptrdiff_t l = 0, r = ok;
      T pivot = lo[ok];
      do {
        ptrdiff_t m = (l + r) >> 1;
        if (lt(pivot, lo[m])) {
          r = m;
        } else {
          l = m + 1;
        }
      } while (l < r);
      for (ptrdiff_t m = ok; m > l; --m) lo[m] = lo[m - 1];
      lo[l] = pivot;
    }
  }

  ptrdiff_t gallop_left(const T& key, const T* a, ptrdiff_t n, ptrdiff_t hint) {
    const T* p = a + hint;
    ptrdiff_t lastofs = 0, ofs = 1;
    if (lt(*p, key)) {
      const ptrdiff_t maxofs = n - hint;
      while (ofs < maxofs) {
        if (lt(p[ofs], key)) {
          lastofs = ofs;
          ofs = (ofs << 1) + 1;
        } else {
          break;
        }
      }
      if (ofs > maxofs) ofs = maxofs;
      lastofs += hint;
      ofs += hint;
    } else {
      const ptrdiff_t maxofs = hint + 1;
      while (ofs < maxofs) {
        if (lt(*(p - ofs), key)) break;
        lastofs = ofs;
        ofs = (ofs << 1) + 1;
      }
      if (ofs > maxofs) ofs = maxofs;
      ptrdiff_t k = lastofs;
      lastofs = hint - ofs;
      ofs = hint - k;
    }
    ++lastofs;
    while (lastofs < ofs) {
      ptrdiff_t m = lastofs + ((ofs - lastofs) >> 1);
      if (lt(a[m], key)) {
        lastofs = m + 1;
      } else {
        ofs = m;
      }
    }
    return ofs;
  }

  ptrdiff_t gallop_right(const T& key, const T* a, ptrdiff_t n, ptrdiff_t hint) {
    const T* p = a + hint;
    ptrdiff_t lastofs = 0, ofs = 1;
    if (lt(key, *p)) {
      const ptrdiff_t maxofs = hint + 1;
      while (ofs < maxofs) {
        if (lt(key, *(p - ofs))) {
          lastofs = ofs;
          ofs = (ofs << 1) + 1;
        } else {
          break;
        }
      }
      if (ofs > maxofs) ofs = maxofs;
      ptrdiff_t k = lastofs;
      lastofs = hint - ofs;
      ofs = hint - k;
    } else {
      const ptrdiff_t maxofs = n - hint;
      while (ofs < maxofs) {
        if (lt(key, p[ofs])) break;
        lastofs = ofs;
        ofs = (ofs << 1) + 1;
      }
      if (ofs > maxofs) ofs = maxofs;
      lastofs += hint;
      ofs += hint;
    }
    ++lastofs;
    while (lastofs < ofs) {
      ptrdiff_t m = lastofs + ((ofs - lastofs) >> 1);
      if (lt(key, a[m])) {
        ofs = m;
      } else {
        lastofs = m + 1;
      }
    }
    return ofs;
  }

  void merge_lo(T* pa, ptrdiff_t na, T* pb, ptrdiff_t nb) {
    tmp_.assign(pa, pa + na);
    T* dest = pa;
    T* a = tmp_.data();
    T* b = pb;
    ptrdiff_t min_gallop;
    *dest++ = *b++;
    --nb;
    if (nb == 0) goto succeed;
    if (na == 1) goto copy_b;
    min_gallop = min_gallop_;
    for (;;) {
      ptrdiff_t acount = 0, bcount = 0;
      for (;;) {
        if (lt(*b, *a)) {
          *dest++ = *b++;
          ++bcount;
          acount = 0;
          --nb;
          if (nb == 0) goto succeed;
          if (bcount >= min_gallop) break;
        } else {
          *dest++ = *a++;
          ++acount;
          bcount = 0;
          --na;
          if (na == 1) goto copy_b;
          if (acount >= min_gallop) break;
        }
      }
      ++min_gallop;
      do {
        min_gallop -= min_gallop > 1;
        min_gallop_ = min_gallop;
        ptrdiff_t k = gallop_right(T(*b), a, na, 0);
        acount = k;
        if (k) {
          std::copy(a, a + k, dest);
          dest += k;
          a += k;
          na -= k;
          if (na == 1) goto copy_b;
          if (na == 0) goto succeed;
        }
        *dest++ = *b++;
        --nb;
        if (nb == 0) goto succeed;
        k = gallop_left(T(*a), b, nb, 0);
        bcount = k;
        if (k) {
          std::copy(b, b + k, dest);
          dest += k;
          b += k;
          nb -= k;
          if (nb == 0) goto succeed;
        }
        *dest++ = *a++;
        --na;
        if (na == 1) goto copy_b;
      } while (acount >= kMinGallop || bcount >= kMinGallop);
      ++min_gallop;
      min_gallop_ = min_gallop;
    }
  succeed:
    if (na) std::copy(a, a + na, dest);
    return;
  copy_b:
    std::copy(b, b + nb, dest);
    dest[nb] = *a;
  }

  void merge_hi(T* pa, ptrdiff_t na, T* pb, ptrdiff_t nb) {
    tmp_.assign(pb, pb + nb);
    T* dest = pb + nb - 1;
    T* basea = pa;
    T* baseb = tmp_.data();
    T* b = baseb + nb - 1;
    T* a = pa + na - 1;
    ptrdiff_t min_gallop;
    *dest-- = *a--;
    --na;
    if (na == 0) goto succeed;
    if (nb == 1) goto copy_a;
    min_gallop = min_gallop_;
    for (;;) {
      ptrdiff_t acount = 0, bcount = 0;
      for (;;) {
        if (lt(*b, *a)) {
          *dest-- = *a--;
          ++acount;
          bcount = 0;
          --na;
          if (na == 0) goto succeed;
          if (acount >= min_gallop) break;
        } else {
          *dest-- = *b--;
          ++bcount;
          acount = 0;
          --nb;
          if (nb == 1) goto copy_a;
          if (bcount >= min_gallop) break;
        }
      }
      ++min_gallop;
      do {
        min_gallop -= min_gallop > 1;
        min_gallop_ = min_gallop;
        ptrdiff_t k = gallop_right(T(*b), basea, na, na - 1);
        k = na - k;
        acount = k;
        if (k) {
          dest -= k;
          a -= k;
          std::copy_backward(a + 1, a + 1 + k, dest + 1 + k);
          na -= k;
          if (na == 0) goto succeed;
        }
        *dest-- = *b--;
        --nb;
        if (nb == 1) goto copy_a;
        k = gallop_left(T(*a), baseb, nb, nb - 1);
        k = nb - k;
        bcount = k;
        if (k) {
          dest -= k;
          b -= k;
          std::copy(b + 1, b + 1 + k, dest + 1);
          nb -= k;
          if (nb == 1) goto copy_a;
          if (nb == 0) goto succeed;
        }
        *dest-- = *a--;
        --na;
        if (na == 0) goto succeed;
      } while (acount >= kMinGallop || bcount >= kMinGallop);
      ++min_gallop;
      min_gallop_ = min_gallop;
    }
  succeed:
    if (nb) std::copy(baseb, baseb + nb, dest - (nb - 1));
    return;
  copy_a:
    std::copy_backward(a + 1 - na, a + 1, dest + 1);
    dest -= na;
    a -= na;
    *dest = *b;
  }

  void merge_at(size_t i) {
    T* base = list_.data();
    ptrdiff_t pa = pending_[i].base, na = pending_[i].len;
    ptrdiff_t pb = pending_[i + 1].base, nb = pending_[i + 1].len;
    pending_[i].len = na + nb;
    pending_.erase(pending_.begin() + static_cast<ptrdiff_t>(i) + 1);
    ptrdiff_t k = gallop_right(T(base[pb]), base + pa, na, 0);
    pa += k;
    na -= k;
    if (na == 0) return;
    nb = gallop_left(T(base[pa + na - 1]), base + pb, nb, nb - 1);
    if (nb <= 0) return;
    if (na <= nb) {
      merge_lo(base + pa, na, base + pb, nb);
    } else {
      merge_hi(base + pa, na, base + pb, nb);
    }
  }

  static int powerloop(ptrdiff_t s1, ptrdiff_t n1, ptrdiff_t n2, ptrdiff_t n) {
    int result = 0;
    ptrdiff_t a = 2 * s1 + n1;
    ptrdiff_t b = a + n1 + n2;
    for (;;) {
      ++result;
      if (a >= n) {
        a -= n;
        b -= n;
      } else if (b >= n) {
        break;
      }
      a <<= 1;
      b <<= 1;
    }
    return result;
  }

  void found_new_run(ptrdiff_t n2) {
    if (pending_.empty()) return;
    ptrdiff_t s1 = pending_.back().base, n1 = pending_.back().len;
    int power = powerloop(s1, n1, n2, listlen_);
    while (pending_.size() > 1 && pending_[pending_.size() - 2].power > power) merge_at(pending_.size() - 2);
    pending_.back().power = power;
  }

  void force_collapse() {
    while (pending_.size() > 1) {
      size_t n = pending_.size() - 2;
      if (n > 0 && pending_[n - 1].len < pending_[n + 1].len) --n;
      merge_at(n);
    }
  }

  std::vector<T>& list_;
  Less& lt_;
  ptrdiff_t listlen_ = 0;
  ptrdiff_t min_gallop_ = kMinGallop;
  std::vector<Run> pending_;
  std::vector<T> tmp_;
};

}

template <class T, class Less>
void list_sort(std::vector<T>& v, Less lt) {
  sort_detail::Timsort<T, Less> s(v, lt);
  s.run();
}

}
}
