//********************************************************************************************
//*
//*    This file is part of Egoboo.
//*
//*    Egoboo is free software: you can redistribute it and/or modify it
//*    under the terms of the GNU General Public License as published by
//*    the Free Software Foundation, either version 3 of the License, or
//*    (at your option) any later version.
//*
//*    Egoboo is distributed in the hope that it will be useful, but
//*    WITHOUT ANY WARRANTY; without even the implied warranty of
//*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
//*    General Public License for more details.
//*
//*    You should have received a copy of the GNU General Public License
//*    along with Egoboo.  If not, see <http://www.gnu.org/licenses/>.
//*
//********************************************************************************************

/// @file   egolib/Math/Point.hpp
/// @brief  Points.
/// @author Michael Heilmann

#pragma once

#include "egolib/Math/_Tuple.hpp"
#include "egolib/Math/_Generator.hpp"

namespace Ego {
namespace Math {

namespace Internal {

/**
 * @brief
 *  Derived from @a std::true_type if @a _VectorSpace and @a ArgTypes
 *  fulfil the requirements for a constructor of a vector,
 *  and derived from @a std::false_type otherwise.
 * @tparam _VectorSpace
 *  must fulfil the <em>VectorSpace</em> concept
 * @param _ArgTypes
 *  @a ArgTypes must have <tt>_VectorSpaceType::dimensionality()-1</tt> elements which are convertible into values of type @a _VectorSpaceType::ScalarType
 * @author
 *  Michael Heilmann
 * @todo
 *  Fast-fail if the parameters are not convertible into @a _ScalarType.
 */
template <typename _VectorSpaceType, typename ... ArgTypes>
struct PointConstructorEnable
    : public std::conditional<
    (Ego::Core::EqualTo<sizeof...(ArgTypes), _VectorSpaceType::dimensionality() - 1>::value),
    std::true_type,
    std::false_type
    >::type {};

} // namespace Internal

/// @brief A point in the \f$n\f$-dimensional Euclidean space.
/// @tparam _EuclideanSpace the Euclidean space over which the points are defined.
template <typename _VectorSpaceType>
struct Point : public Tuple<typename _VectorSpaceType::ScalarType, _VectorSpaceType::dimensionality()> {
public:
    /// @brief The vector space of this vector.
    typedef _VectorSpaceType VectorSpaceType;
    /// @brief The dimensionality.
    static constexpr size_t dimensionality() {
        return VectorSpaceType::dimensionality();
    }
    /// @brief The scalar field type (of the vector space).
    typedef typename VectorSpaceType::ScalarFieldType ScalarFieldType;
    /// @brief The vector type (of the vector space).
    typedef typename VectorSpaceType::VectorType VectorType;
    /// @brief The scalar type (of the scalar field).
    typedef typename VectorSpaceType::ScalarType ScalarType;

public:
    /// @brief The type of this template/template specialization.
    typedef Point<VectorSpaceType> MyType;
    /// @brief The tuple type.
    typedef Tuple<ScalarType, MyType::dimensionality()> TupleType;

public:


public:
    /**
     * @brief
     *  Construct this point with the specified element values.
     * @param v, ... args
     *  the element values
     */
    template<typename ... ArgTypes, typename = std::enable_if_t<Internal::PointConstructorEnable<VectorSpaceType, ArgTypes ...>::value>>
    Point(ScalarType v, ArgTypes&& ... args)
        : TupleType(std::forward<ScalarType>(v), args ...) {
        /* Intentionally empty. */
    }

    /**
     * @brief
     *  Copy-construct this poiint with the values of another point.
     * @param other
     *  the other point
     */
    Point(const MyType& other)
        : TupleType(other) {
        /* Intentionally empty. */
    }

protected:
    /**
     * @brief
     *  Construct this point with the values generated by a sequence generator.
     * @tparam _GeneratorType
     *  the generator type
     * @tparam ... Index
     *  indices 0, 1, ..., dimensionality() - 1
     */
    template <typename _GeneratorType, size_t ... Index>
    Point(const _GeneratorType& generator, std::index_sequence<Index ...>)
        : Point(generator(Index) ...) {}

public:
    /**
     * @brief
     *  Default-construct this point.
     */
    Point()
        : Point(ConstantGenerator<ScalarType>(ScalarFieldType::additiveNeutral()),
                std::make_index_sequence<VectorSpaceType::dimensionality()>{}) {
        /* Intentionally empty. */
    }

public:
    inline ScalarType& x() {
        static_assert(VectorSpaceType::dimensionality() >= 1, "cannot call for member x() with dimensionality less than 1");
        return this->_elements[0];
    }

    inline ScalarType& y() {
        static_assert(VectorSpaceType::dimensionality() >= 2, "cannot call for member y() with dimensionality less than 2");
        return this->_elements[1];
    }

    inline ScalarType& z() {
        static_assert(VectorSpaceType::dimensionality() >= 3, "cannot call for member z() with dimensionality less than 3");
        return this->_elements[2];
    }

    inline const ScalarType& x() const {
        static_assert(VectorSpaceType::dimensionality() >= 1, "cannot call for member x() with dimensionality less than 1");
        return this->_elements[0];
    }

    inline const ScalarType& y() const {
        static_assert(VectorSpaceType::dimensionality() >= 2, "cannot call for member y() with dimensionality less than 2");
        return this->_elements[1];
    }

    inline const ScalarType& z() const {
        static_assert(VectorSpaceType::dimensionality() >= 3, "cannot call for member z() with dimensionality less than 3");
        return this->_elements[2];
    }

public:
    /**
     * @brief
     *  Assign this point with the values of another point.
     * @param other
     *  the other point
     */
    void assign(const MyType& other) {
        TupleType::assign(other);
    }

    /**
     * @brief
     *  Set all elements in this point to zero.
     * @todo
     *  Remove this.
     */
    void setZero() {
        (*this) = MyType();
    }

public:
    /**
     * @brief
     *  Add a vector to this point,
     *  assign the result to this point.
     * @param other
     *  the vector
     * @post
     *  The sum <tt>(*this) + other</tt> was assigned to <tt>*this</tt>.
     */
    void add(const VectorType& other) {
        for (size_t i = 0; i < MyType::dimensionality(); ++i) {
            this->_elements[i] = ScalarFieldType::sum(this->_elements[i], other[i]);
        }
    }

    /**
     * @brief
     *  Subtract a vector from this point,
     *  assign the result to this point.
     * @param other
     *  the vector
     * @post
     *  The difference <tt>(*this) - other</tt> was assigned to <tt>*this</tt>.
     */
    void sub(const VectorType& other) {
        for (size_t i = 0; i < MyType::dimensionality(); ++i) {
            this->_elements[i] = ScalarFieldType::difference(this->_elements[i], other[i]);
        }
    }

public:
    /**
    * @brief
    *  Get if this point equals another point.
    * @param other
    *  the other point
    * @return
    *  @a true if this point equals the other point
    */
    bool equals(const MyType& other) const {
        for (size_t i = 0; i < MyType::dimensionality(); ++i) {
            if (ScalarFieldType::notEqualTo(this->_elements[i], other._elements[i])) {
                return false;
            }
        }
        return true;
    }

    /**
     * @brief
     *  Get if this point equals another point.
     * @param other
     *  the other point
     * @param ulp
     *  see ScalarFieldType::notEqualUlp
     * @return
     *  @a true if this point equals the other point
     */
    bool equalsUlp(const MyType& other, const size_t& ulp) const {
        for (size_t i = 0; i < MyType::dimensionality(); ++i) {
            if (ScalarFieldType::notEqualULP(this->_elements[i], other._elements[i], ulp)) {
                return false;
            }
        }
        return true;
    }

    /**
     * @brief
     *  Get if this point equals another point.
     * @param other
     *  the other point
     * @param tolerance
     *  see ScalarFieldType::notEqualTolerance
     * @return
     *  @a true if this point equals the other point
     */
    bool equalsTolerance(const MyType& other, const ScalarType& tolerance) const {
        for (size_t i = 0; i < MyType::dimensionality(); ++i) {
            if (ScalarFieldType::notEqualTolerance(this->_elements[i], other._elements[i], tolerance)) {
                return false;
            }
        }
        return true;
    }

public:
    bool operator==(const MyType& other) const {
        return equals(other);
    }

    bool operator!=(const MyType& other) const {
        return !equals(other);
    }

public:
    // As always, return non-const reference in order to allow chaining for the sake of orthogonality.
    MyType& operator=(const MyType& other) {
        assign(other);
        return *this;
    }

public:
    MyType& operator+=(const VectorType& other) {
        add(other);
        return *this;
    }

    MyType& operator-=(const VectorType& other) {
        sub(other);
        return *this;
    }

public:
    MyType operator+(const VectorType& other) const {
        MyType t(*this);
        t += other;
        return t;
    }

    MyType operator-(const VectorType& other) const {
        MyType t(*this);
        t -= other;
        return t;
    }

private:
    /** @internal */
    ScalarType sub(const MyType& other, size_t index) const {
        return ScalarFieldType::difference(this->_elements[index], other._elements[index]);
    }
    /** @internal */
    template <size_t... Index>
    VectorType sub(const MyType& other, std::index_sequence<Index ...>) const {
        return VectorType((sub(other, Index))...);
    }
    VectorType sub(const MyType& other) const {
        return sub(other, std::make_index_sequence<VectorSpaceType::dimensionality()>{});
    }

public:
    VectorType operator-(const MyType& other) const {
        return sub(other);
    }

public:
    ScalarType& operator[](size_t const& index) {
        return this->at(index);
    }

    const ScalarType& operator[](size_t const& index) const {
        return this->at(index);
    }

public:
    /**
     * @brief
     *  Get the zero point.
     * @return
     *  the zero point
     */
    static const MyType& zero() {
        static ConstantGenerator<ScalarType> g(ScalarFieldType::additiveNeutral());
        static const auto v = MyType(g, std::make_index_sequence<VectorSpaceType::dimensionality()>{});
        return v;
    }


}; // struct Point

} // namespace Math
} // namespace Ego
