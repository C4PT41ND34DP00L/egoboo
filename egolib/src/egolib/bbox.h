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

/// @file  egolib/bbox.h
/// @brief A small "library" for dealing with various bounding boxes.

#pragma once

#include "egolib/Math/_Include.hpp"

//--------------------------------------------------------------------------------------------

/// Level 0 character "bumper"
/// The simplest collision volume, equivalent to the old-style collision data
/// stored in data.txt
    struct bumper_t
    {
        float  size;        ///< Size of bumpers
        float  size_big;    ///< For octagonal bumpers
        float  height;      ///< Distance from head to toe

        bumper_t() :
            size(0.0f),
            size_big(0.0f),
            height(0.0f)
        {
            //ctor
        }

        static void reset(bumper_t *self)
        {
            self->size = 0.0f;
            self->size_big = 0.0f;
            self->height = 0.0f;
        }
    };

//--------------------------------------------------------------------------------------------

/// The various axes for the octagonal bounding box
    enum e_octagonal_axes
    {
        OCT_X, OCT_Y, OCT_Z, OCT_XY, OCT_YX, OCT_COUNT
    };

    struct oct_vec_v2_t
    {

    public:

        float _v[OCT_COUNT];

        static const oct_vec_v2_t Zero;

    public:

        void add(const Vector3f& other)
        {
            add(oct_vec_v2_t(other));
        }

        void add(const oct_vec_v2_t& other)
        {
            for (size_t i = 0; i < OCT_COUNT; ++i)
            {
                _v[i] += other[i];
            }
        }

        void sub(const Vector3f& other)
        {
            sub(oct_vec_v2_t(other));
        }

        void sub(const oct_vec_v2_t& other)
        {
            for (size_t i = 0; i < OCT_COUNT; ++i)
            {
                _v[i] -= other[i];
            }
        }

        void mul(const float scalar)
        {
            for (size_t i = 0; i < OCT_COUNT; ++i)
            {
                _v[i] *= scalar;
            }
        }

        void assign(const oct_vec_v2_t& other)
        {
            for (size_t i = 0; i < OCT_COUNT; ++i)
            {
                _v[i] = other._v[i];
            }
        }

    public:

        oct_vec_v2_t()
            : oct_vec_v2_t(0.0f, 0.0f, 0.0f, 0.0f, 0.0f)
        { }

		oct_vec_v2_t(const Vector3f& point)
            : oct_vec_v2_t(point[kX], point[kY], point[kZ], point[kX] + point[kY], point[kY] - point[kX])
        { }

        oct_vec_v2_t(float x, float y, float z, float xy, float yx)
            : _v { x, y, z, xy, yx }
        { }

        oct_vec_v2_t(const oct_vec_v2_t& other)
        {
            for (size_t i = 0; i < OCT_COUNT; ++i)
            {
                _v[i] = other._v[i];
            }
        }

    public:

        oct_vec_v2_t operator+(const oct_vec_v2_t& other) const
        {
            return
                oct_vec_v2_t
                (
                    _v[OCT_X]  + other._v[OCT_X],
                    _v[OCT_Y]  + other._v[OCT_Y],
                    _v[OCT_Z]  + other._v[OCT_Z],
                    _v[OCT_XY] + other._v[OCT_XY],
                    _v[OCT_YX] + other._v[OCT_YX]
                );
        }

        oct_vec_v2_t& operator+=(const oct_vec_v2_t& other)
        {
            add(other);
            return *this;
        }

    public:

        oct_vec_v2_t operator-(const oct_vec_v2_t& other) const
        {
            return
                oct_vec_v2_t
                (
                    _v[OCT_X]  - other._v[OCT_X],
                    _v[OCT_Y]  - other._v[OCT_Y],
                    _v[OCT_Z]  - other._v[OCT_Z],
                    _v[OCT_XY] - other._v[OCT_XY],
                    _v[OCT_YX] - other._v[OCT_YX]
                );
        }

        oct_vec_v2_t& operator-=(const oct_vec_v2_t& other)
        {
            sub(other);
            return *this;
        }

    public:

        oct_vec_v2_t& operator*=(const float scalar)
        {
            mul(scalar);
            return *this;
        }

        oct_vec_v2_t operator*(const float scalar) const
        {
            return
                oct_vec_v2_t
                (
                _v[OCT_X]  * scalar,
                _v[OCT_Y]  * scalar,
                _v[OCT_Z]  * scalar,
                // x * scalar + y * scalar = (x + y) * scalar
                _v[OCT_XY] * scalar,
                // y * scalar - x * scalar = (y - x) * scalar
                _v[OCT_YX] * scalar
                );
        }

    public:

        oct_vec_v2_t& operator=(const oct_vec_v2_t& other)
        {
            assign(other);
            return *this;
        }

    public:

        const float& operator[] (const size_t index) const
        {
            if (index >= OCT_COUNT)
            {
                throw std::out_of_range("index out of range");
            }
            return _v[index];
        }

        float& operator[](const size_t index)
        {
            if (index >= OCT_COUNT)
            {
                throw std::out_of_range("index out of range");
            }
            return _v[index];
        }
        
    };

//--------------------------------------------------------------------------------------------

    /**
     * @brief
     *  An octagonal bounding box.
     * @remark
     *  Note that Egoboo still considers the z - axis as the "up" axis. Given that,
     *  the following bounding volume is representing an octagonal prism in terms
     *  of the intersection of an axis-aligned bounding box and a diamond prism.
     *  This is currently the best (*cry*) bounding volume Egoboo has available and
     *  is computed on the fly from the model or mesh data.
     * @see
     *  http ://mathworld.wolfram.com/Prism.html
     */
    struct oct_bb_t
    {
    public:
        bool _empty;

    public:
        oct_vec_v2_t _mins, _maxs;

        bool isEmpty() const
        {
            return _empty;
        }

	public:

        oct_bb_t() :
            _empty(true),
            _mins(),
            _maxs() {
        }

		friend id::translate_functor<oct_bb_t, oct_vec_v2_t>;

	protected:
		oct_bb_t(bool empty, const oct_vec_v2_t& min, const oct_vec_v2_t& max) :
			_empty(empty),
			_mins(min),
			_maxs(max)
		{}

	public:
		oct_bb_t(const oct_vec_v2_t& other) :
			_empty(false),
			_mins(other),
			_maxs(other) {
		}

        oct_bb_t(const oct_bb_t& other) :
            _empty(other._empty),
            _mins(other._mins), 
            _maxs(other._maxs) {
        }

		oct_bb_t(const bumper_t& other) {
			_mins[OCT_X] = -other.size;
			_maxs[OCT_X] = +other.size;

			_mins[OCT_Y] = -other.size;
			_maxs[OCT_Y] = +other.size;

			_mins[OCT_XY] = -other.size_big;
			_maxs[OCT_XY] = +other.size_big;

			_mins[OCT_YX] = -other.size_big;
			_maxs[OCT_YX] = +other.size_big;

			_mins[OCT_Z] = -other.height;
			_maxs[OCT_Z] = +other.height;

			_empty = false;
		}

	public:

        void assign(const oct_bb_t& other) {
            _mins = other._mins;
            _maxs = other._maxs;
            _empty = other._empty;
        }

        oct_bb_t& operator=(const oct_bb_t& other) {
            assign(other);
            return *this;
        }

        const oct_vec_v2_t& getMin() const {
            if (_empty) {
                throw std::invalid_argument("an empty obb does not have a min-point");
            }
            return _mins;
        }

        const oct_vec_v2_t& getMax() const {
            if (_empty) {
                throw std::invalid_argument("an empty obb does not have a max-point");
            }
            return _maxs;
        }

        oct_vec_v2_t getMid() const {
            if (_empty) {
                throw std::invalid_argument("an empty obb does not have a mid-point");
            }
            return (_mins + _maxs) * 0.5f;
        }
        
        /**
         * @brief
         *  Get the axis-aligned bounding box aspect of this octagonal bounding box.
         * @return
         *  the axis-aligned bounding box
         */
		AxisAlignedBox3f toAxisAlignedBox() const {
            if (_empty) {
                throw std::logic_error("unable to convert an empty OBB into an AABB");
            }
            return AxisAlignedBox3f(Point3f(_mins[OCT_X], _mins[OCT_Y], _mins[OCT_Z]),
				                    Point3f(_maxs[OCT_X], _maxs[OCT_Y], _maxs[OCT_Z]));
        }

        void assign(const bumper_t& other)
        {
            _mins[OCT_X] = -other.size;
            _maxs[OCT_X] = +other.size;

            _mins[OCT_Y] = -other.size;
            _maxs[OCT_Y] = +other.size;

            _mins[OCT_XY] = -other.size_big;
            _maxs[OCT_XY] = +other.size_big;

            _mins[OCT_YX] = -other.size_big;
            _maxs[OCT_YX] = +other.size_big;

            _mins[OCT_Z] = -other.height;
            _maxs[OCT_Z] = +other.height;

			_empty = false;
        }

        /**
         * @brief
         *  Get if this bounding volume contains a point.
         * @param self
         *  this bounding volume
         * @param other
         *  the point
         * @return
         *  @a true if this bounding volume contains the point, @a false otherwise
         */
        static bool contains(const oct_bb_t& self, const oct_vec_v2_t& other);
        
        /**
         * @brief
         *  Get if this bounding volume contains another bounding volume.
         * @param self
         *  this bounding volume
         * @param other
         *  the other bounding volume
         * @return
         *  @a true if this bounding volume contains the other bounding volume, @a false otherwise
         */
        static bool contains(const oct_bb_t& self, const oct_bb_t& other);

        static egolib_rv validate(oct_bb_t& self);
        static bool empty_raw(const oct_bb_t& self);

	public:

        /**
         * @brief
         *  Assign this octagonal bounding box the join of itself with a octagonal vector.
         * @param self
         *  this octagonal bounding box
         * @param other
         *  the octagonal vector
         * @post
         *  This octagonal bounding box was assigned the join of itself with the octagonal vector.
         */
        void join(const oct_vec_v2_t& other);

        /**
         * @brief
         *  Assign this octagonal bounding box the join of itself with a another octagonal bounding box.
         * @param other
         *  the other octagonal bounding box
         * @post
         *  This octagonal bounding box was assigned the join of itself with the other octagonal bounding box.
         */
        void join(const oct_bb_t& other);

		/**
		 * @brief
		 *  Assign this octagonal bounding box the restricted join of itself with another octagonal bounding box.
		 * @param other
		 *  the other octagonal bounding box
		 * @param index
		 *  the axis the join is restricted to
		 * @post
		 *  This octagonal bounding box was asisigned the restricted join of itself with the other octagonal bounding box.
		 */
		void join(const oct_bb_t& other, int index);

		static void join(const oct_bb_t& src1, const oct_bb_t& src2, oct_bb_t& dst);

	public:

        /**
         * @brief
         *  Assign this octagonal bounding box the cut of itself with a another octagonal bounding box.
         * @param other
         *  the other octagonal bounding box
         * @post
         *  This octagonal bounding box was assigned the cut of itself with the other octagonal bounding box.
         */
        egolib_rv cut(const oct_bb_t& other);

        /**
         * @brief
         *  Assign this octagonal bounding box the restricted cut of itself with another octagonal bounding box.
         * @param other
         *  the other octagonal bounding box
         * @param index
         *  the axis the join is restricted to
         * @post
         *  This octagonal bounding box was assigned the restricted cut of itself with the other octagonal bounding box.
         */
        egolib_rv cut(const oct_bb_t& other, int index);

		static egolib_rv downgrade(const oct_bb_t& psrc_bb, const bumper_t& bump_stt, const bumper_t& bump_base, oct_bb_t& pdst_bb);
		static egolib_rv downgrade(const oct_bb_t& psrc_bb, const bumper_t& bump_stt, const bumper_t& bump_base, bumper_t& pdst_bump);


		static oct_bb_t intersection(const oct_bb_t& src1, const oct_bb_t& src2);

		static oct_bb_t interpolate(const oct_bb_t& src1, const oct_bb_t& src2, float flip);

		static void validate_index(oct_bb_t& self, int index);
		static bool empty_index_raw(const oct_bb_t& self, int index);
		static bool empty_index(const oct_bb_t& self, int index);
		static void copy(oct_bb_t& dst, const oct_bb_t& src);

		static void self_grow(oct_bb_t& self, const oct_vec_v2_t& v);

		static int to_points(const oct_bb_t& self, Vector4f pos[], size_t pos_count);
		static void points_to_oct_bb(oct_bb_t& self, const Vector4f pos[], const size_t pos_count);
		static bool empty(const oct_bb_t& self);
    };

namespace id {

template <>
struct translate_functor<oct_bb_t, oct_vec_v2_t>
{
	oct_bb_t operator()(const oct_bb_t& x, const oct_vec_v2_t& t) const
	{ return oct_bb_t(x._empty, x._mins + t, x._maxs + t); }
}; // struct translate_functor

template <>
struct translate_functor<oct_bb_t, Vector3f>
{
	oct_bb_t operator()(const oct_bb_t& x, const ::Vector3f& t) const
	{ return translate(x, oct_vec_v2_t(t)); }
}; // struct translate_functor

} // namespace id
