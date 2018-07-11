/*
 * tests/wobbly_test.cpp
 *
 * Copyright 2018 Endless Mobile, Inc.
 *
 * libwobbly is free software: you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 2.1 of the
 * License, or (at your option) any later version.
 *
 * libwobbly is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with eos-companion-app-service.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * Tests for the "wobbly" spring model.
 */
#include <algorithm>                    // for max
#include <array>                        // for array, array<>::value_type
#include <functional>                   // for function, __bind, __base, etc
#include <memory>                       // for unique_ptr
#include <sstream>                      // for operator<<, ostream, etc
#include <vector>                       // for vector

#include <cstddef>                      // for size_t
#include <stdlib.h>                     // for exit
#include <math.h>                       // for ceil

#include <gmock/gmock-cardinalities.h>  // for AtLeast
#include <gmock/gmock-generated-function-mockers.h>  // for FunctionMocker, etc
#include <gmock/gmock-matchers.h>       // for EXPECT_THAT, etc
#include <gmock/gmock-spec-builders.h>  // for EXPECT_CALL, etc
#include <gmock/gmock.h>                // IWYU pragma: keep
#include <gtest/gtest-death-test.h>     // for DeathTest, ExitedWithCode, etc
#include <gtest/gtest-param-test.h>     // for Values, etc
#include <gtest/gtest-typed-test.h>     // for TYPED_TEST, etc
#include <gtest/gtest.h>                // for AssertHelper, etc

#include <wobbly/wobbly.h>    // for Point, PointView, Vector, etc
#include <wobbly/wobbly_internal.h>            // for MeshArray, SpringMesh, etc

#include <mathematical_model_matcher.h>  // for Eq, EqDispatchHelper, etc
#include <ostream_point_operator.h>     // for operator<<
#include <within_geometry_matcher.h>

using ::testing::_;
using ::testing::AtLeast;
using ::testing::ElementsAreArray;
using ::testing::ExitedWithCode;
using ::testing::MakePolymorphicMatcher;
using ::testing::Matcher;
using ::testing::MatchResultListener;
using ::testing::Not;
using ::testing::PolymorphicMatcher;
using ::testing::Return;
using ::testing::Test;
using ::testing::Types;
using ::testing::ValuesIn;
using ::testing::WithParamInterface;

using ::wobbly::matchers::Eq;
using ::wobbly::matchers::SatisfiesModel;
using ::wobbly::matchers::WithinGeometry;

using ::wobbly::models::Parabolic;

namespace
{
    namespace wgd = wobbly::geometry::dimension;

    class SingleObjectStorage
    {
        public:

            SingleObjectStorage ()
            {
                storage.fill (0);
            }

            wobbly::PointView <double> Position ()
            {
                return wobbly::PointView <double> (storage, 0);
            }

            wobbly::PointView <double> Velocity ()
            {
                return wobbly::PointView <double> (storage, 1);
            }

            wobbly::PointView <double> Force ()
            {
                return wobbly::PointView <double> (storage, 2);
            }

        private:

            std::array <double, 6> storage;
    };

    class SingleObjectStorageView
    {
        public:

            SingleObjectStorageView (SingleObjectStorage &storage) :
                position (storage.Position ()),
                velocity (storage.Velocity ()),
                force (storage.Force ())
            {
            }

            wobbly::PointView <double> position;
            wobbly::PointView <double> velocity;
            wobbly::PointView <double> force;
    };

    constexpr double SpringConstant = 0.5f;

    double TileWidth (double width)
    {
        return width / (wobbly::config::Width - 1);
    }

    double TileHeight (double height)
    {
        return height / (wobbly::config::Height - 1);
    }

    double const EvenSize = 100.0f;

    class EvenlyDistributedMesh :
        public Test
    {
        public:

            EvenlyDistributedMesh () :
                mesh (GetArray ())
            {
            }

            static wobbly::MeshArray GetArray ()
            {
                wobbly::MeshArray mesh;

                wobbly::Vector size (TileWidth (EvenSize),
                                     TileHeight (EvenSize));
                wobbly::mesh::CalculatePositionArray (wobbly::Point (0, 0),
                                                      mesh,
                                                      size);

                return mesh;
            }

            wobbly::MeshArray mesh;
    };

    struct ClosestIndexToPositionParam
    {
        wobbly::Point point;
        size_t        expectedIndex;
    };

    class ClosestIndexToPosition :
        public EvenlyDistributedMesh,
        public WithParamInterface <ClosestIndexToPositionParam>
    {
    public:

        static std::vector <ParamType> NoTransform ()
        {
            return GetParams ([](wobbly::Point const &p) {
            });
        }

        static std::vector <ParamType> Expanded ()
        {
            wobbly::Vector translation (EvenSize / 2, EvenSize / 2);

            return GetParams ([&translation](wobbly::Point &point) {
                 /* Scale on center */
                wgd::pointwise_subtract (point, translation);
                /* Older versions of cppcheck have trouble seeing through
                 * the lambda */
                // cppcheck-suppress unreachableCode
                wgd::scale (point, 1.1);
                wgd::pointwise_add (point, translation);
            });
        }

        static std::vector <ParamType> Shrinked ()
        {
            wobbly::Vector translation (EvenSize / 2, EvenSize / 2);

            return GetParams ([&translation](wobbly::Point &point) {
                /* Scale on center */
                wgd::pointwise_subtract (point, translation);
                /* Older versions of cppcheck have trouble seeing through
                 * the lambda */
                // cppcheck-suppress unreachableCode
                wgd::scale (point, 1.0 / 1.1);
                wgd::pointwise_add (point, translation);
            });
        }

    private:

        template <typename Transformation>
        static std::vector <ParamType> GetParams (Transformation const &trans)
        {
            std::vector <ParamType> vec;
            auto array (GetArray ());

            auto const total = wobbly::config::TotalIndices;

            for (size_t i = 0; i < total; ++i)
            {
                wobbly::PointView <double> pv (array, i);
                ParamType param;

                wgd::assign (param.point, pv);
                param.expectedIndex = i;

                trans (param.point);

                vec.push_back (param);
            }

            return vec;
        }
    };

    TEST_P (ClosestIndexToPosition, Find)
    {
        EXPECT_EQ (GetParam ().expectedIndex,
                   wobbly::mesh::ClosestIndexToPosition (mesh,
                                                         GetParam ().point));
    }

    /* First case is just the mesh points themselves */
    INSTANTIATE_TEST_CASE_P (MeshPoints, ClosestIndexToPosition,
                             ValuesIn (ClosestIndexToPosition::NoTransform ()));

    /* Second case is the mesh points scaled outwards by a little bit */
    INSTANTIATE_TEST_CASE_P (ExpandedPoints, ClosestIndexToPosition,
                             ValuesIn (ClosestIndexToPosition::Expanded ()));

    /* Third case is the mesh points scaled outwards by a little bit */
    INSTANTIATE_TEST_CASE_P (ShrinkedPoints, ClosestIndexToPosition,
                             ValuesIn (ClosestIndexToPosition::Shrinked ()));

    class SpringMesh :
        public EvenlyDistributedMesh
    {
    public:

        SpringMesh () :
            firstPreference ([](wobbly::Spring const &spring) {
                return spring.FirstPosition ();
            }),
            secondPreference ([](wobbly::Spring const &spring) {
                return spring.SecondPosition ();
            }),
            springMesh (mesh,
                        wobbly::Vector (TileWidth (EvenSize),
                                        TileHeight (EvenSize)))
        {
        }

        static void ApplyMovement (std::unique_ptr <double[]> &ptr,
                                   wobbly::Vector const       &movement)
        {
            wobbly::PointView <double> pv (ptr.get (), 0);
            wgd::pointwise_add (pv, movement);
        }

        wobbly::SpringMesh::PosPreference firstPreference;
        wobbly::SpringMesh::PosPreference secondPreference;

        wobbly::SpringMesh  springMesh;
    };

    template <typename P1, typename P2>
    class PointsInSameDirectionMatcher
    {
        public:

            PointsInSameDirectionMatcher (P1 const &origin,
                                          P2 const &candidate) :
                origin (origin),
                candidate (candidate)
            {
            }

            template <typename Vector>
            bool MatchAndExplain (Vector        const &vec,
                                  MatchResultListener *listener) const
            {
                /* A vector points towards another point if tan(theta) formed
                 * by the two components is the same as tan(theta) formed by the
                 * component distance from candidate to origin */

                wobbly::Vector delta;
                wgd::assign (delta, candidate);
                wgd::pointwise_subtract (delta, origin);

                auto vecTanTheta = (wgd::get <1> (vec) /
                                    wgd::get <0> (vec));
                auto distTanTheta = (wgd::get <1> (delta) /
                                     wgd::get <0> (delta));

                if (listener)
                    *listener << "vector (" << vec << ")'s tan(theta) is "
                              << vecTanTheta << " and tan(theta) between origin"
                              << " (" << origin << ") and candidate ("
                              << candidate << ")" << " with delta ("
                              << delta << ") is " << distTanTheta;

                return wobbly::testing::close_at_tolerance (vecTanTheta, distTanTheta, 10e-7);
            }

            void DescribeTo (::std::ostream *os) const
            {
                *os << "point in the same direction as the line formed by "
                    << origin
                    << " and "
                    << candidate;
            }

            void DescribeNegationTo (::std::ostream *os) const
            {
                *os << "does not ";
                DescribeTo (os);
            }

        private:

            P1 const &origin;
            P2 const &candidate;
    };

    template <typename P1, typename P2>
    inline PolymorphicMatcher <PointsInSameDirectionMatcher <P1, P2> >
    PointsInSameDirection (P1 const &origin, P2 const &candidate)
    {
        PointsInSameDirectionMatcher <P1, P2> matcher (origin, candidate);
        return MakePolymorphicMatcher (matcher);
    }

    /* We can observe the insertion of temporary anchors by looking at
     * the direction of a first application of force. */

    /* When a temporary anchor is inserted, the points on which
     * the anchor was inserted should have a net force pointing
     * towards the anchor's left or right neighbour */
    TEST_F (SpringMesh, ForceOnPointsTowardsTemporaryAnchor)
    {
        /* Insert a temporary anchor between points (1) and (2) on the grid */
        wobbly::Point const install (EvenSize / 2, 0);
        wobbly::Point const movement (25, -50);
        auto handle (springMesh.InstallAnchorSprings (install,
                                                      firstPreference,
                                                      secondPreference));
        ApplyMovement (handle.data, movement);

        auto result = springMesh.CalculateForces (SpringConstant);

        wobbly::Point anchorPosition (install);
        wgd::pointwise_add (anchorPosition, movement);

        wobbly::Vector const leftOffset (-TileWidth (EvenSize) / 2, 0);
        wobbly::Vector const rightOffset (TileWidth (EvenSize) / 2, 0);

        wobbly::Point anchorLeftPoint (anchorPosition);
        wgd::pointwise_add (anchorLeftPoint, leftOffset);

        wobbly::Point anchorRightPoint (anchorPosition);
        wgd::pointwise_add (anchorRightPoint, rightOffset);

        wobbly::PointView <double const> firstPoint (mesh, 1);
        wobbly::PointView <double const> secondPoint (mesh, 2);

        EXPECT_THAT (wobbly::PointView <double const> (result.forces, 1),
                     PointsInSameDirection (firstPoint, anchorLeftPoint));
        EXPECT_THAT (wobbly::PointView <double const> (result.forces, 2),
                     PointsInSameDirection (secondPoint, anchorRightPoint));
    }

    /* Insert a second temporary anchor in between a temporary
     * anchor and an actual point on the grid. The result should be that
     * upon moving the second temporary anchor and applying forces
     * for the first time that a force will be directed towards the first
     * anchor from the first base point (1) and towards the second anchor
     * from the second base point (2) */
    TEST_F (SpringMesh, InsertTemporaryAnchorOnTemporarySpring)
    {
        /* Insert first temporary anchor between points (1) and (2) on
         * the grid. */
        wobbly::Point const firstInstall (EvenSize / 2, 0);
        auto firstHandle (springMesh.InstallAnchorSprings (firstInstall,
                                                           firstPreference,
                                                           secondPreference));
        /* Insert second temporary anchor between the first temporary
         * spring's anchor and base point (2) */
        wobbly::Point const secondInstall (EvenSize / 2 +
                                               (TileWidth (EvenSize) / 4),
                                           0);
        auto secondHandle (springMesh.InstallAnchorSprings (secondInstall,
                                                            firstPreference,
                                                            secondPreference));

        /* Move first handle to point above point (1) and second anchor
         * to point above point (2) */
        wobbly::Vector const firstMovement (-25, -50);
        wobbly::Vector const secondMovement (25, -50);

        wobbly::Point firstAnchorPoint (firstInstall);
        wgd::pointwise_add (firstAnchorPoint, firstMovement);
        ApplyMovement (firstHandle.data, firstMovement);

        wobbly::Point secondAnchorPoint (secondInstall);
        wgd::pointwise_add (secondAnchorPoint, secondMovement);
        ApplyMovement (secondHandle.data, secondMovement);

        /* Calculate forces */
        auto result = springMesh.CalculateForces (SpringConstant);

        /* The desired delta between the first point and its base neighbour is
         * TileWidth / 2, 0 */
        wobbly::Point leftOfFirstAnchor (-TileWidth (EvenSize) / 2, 0);
        wgd::pointwise_add (leftOfFirstAnchor, firstAnchorPoint);

        /* For the second point, because we inserted it between the first
         * spring and the second base point, the desired delta will be half
         * of the first spring length, eg, TileWidth (EvenSize) / 4, 0 */
        wobbly::Point rightOfSecondAnchor (TileWidth (EvenSize) / 4, 0);
        wgd::pointwise_add (rightOfSecondAnchor, secondAnchorPoint);

        wobbly::PointView <double const> firstPoint (mesh, 1);
        wobbly::PointView <double const> secondPoint (mesh, 2);

        EXPECT_THAT (wobbly::PointView <double const> (result.forces, 1),
                     PointsInSameDirection (firstPoint, leftOfFirstAnchor));
        EXPECT_THAT (wobbly::PointView <double const> (result.forces, 2),
                     PointsInSameDirection (secondPoint, rightOfSecondAnchor));
    }

    /* Same setup as the previous test, but this time move the anchors around
     * but let the second one expire. The force should revert back to the way
     * it was in the first test */
    TEST_F (SpringMesh, OnDestructionOfTemporarySpringForceRevertsToFirstAnchor)
    {
        typedef wobbly::SpringMesh::InstallResult IR;
        typedef std::unique_ptr <wobbly::SpringMesh::InstallResult> Handle;

        auto &fp (firstPreference);
        auto &sp (secondPreference);

        /* Insert first temporary anchor between points (1) and (2) on
         * the grid. */
        wobbly::Point const firstInstall (EvenSize / 2, 0);
        Handle first (new IR (springMesh.InstallAnchorSprings (firstInstall,
                                                               fp,
                                                               sp)));

        /* Insert second temporary anchor between the first temporary
         * spring's anchor and base point (2) */
        wobbly::Point const secondInstall (EvenSize / 2 +
                                               (TileWidth (EvenSize) / 4),
                                           0);
        Handle second (new IR (springMesh.InstallAnchorSprings (secondInstall,
                                                                fp,
                                                                sp)));

        /* Move first handle to point above point (1) and second anchor
         * to point above point (2) */
        wobbly::Vector const firstMovement (-25, -50);
        wobbly::Vector const secondMovement (25, -50);

        wobbly::Point firstAnchorPoint (firstInstall);
        wgd::pointwise_add (firstAnchorPoint, firstMovement);
        ApplyMovement (first->data, firstMovement);

        wobbly::Point secondAnchorPoint (secondInstall);
        wgd::pointwise_add (secondAnchorPoint, secondMovement);
        ApplyMovement (second->data, secondMovement);

        /* Now that both handles have been moved, make the second one expire */
        second.reset ();

        /* Calculate forces */
        auto result = springMesh.CalculateForces (SpringConstant);

        wobbly::Point anchorPosition (firstInstall);
        wgd::pointwise_add (anchorPosition, firstMovement);

        wobbly::Vector const leftOffset (-TileWidth (EvenSize) / 2, 0);
        wobbly::Vector const rightOffset (TileWidth (EvenSize) / 2, 0);

        wobbly::Point anchorLeftPoint (firstAnchorPoint);
        wgd::pointwise_add (anchorLeftPoint, leftOffset);

        wobbly::Point anchorRightPoint (firstAnchorPoint);
        wgd::pointwise_add (anchorRightPoint, rightOffset);

        wobbly::PointView <double const> firstPoint (mesh, 1);
        wobbly::PointView <double const> secondPoint (mesh, 2);

        EXPECT_THAT (wobbly::PointView <double const> (result.forces, 1),
                     PointsInSameDirection (firstPoint, anchorLeftPoint));
        EXPECT_THAT (wobbly::PointView <double const> (result.forces, 2),
                     PointsInSameDirection (secondPoint, anchorRightPoint));
    }

    TEST_F (SpringMesh, HandlesCanExpireInNonReverseOrder)
    {
        EXPECT_EXIT ({
            wobbly::Point const install (EvenSize / 2, 0);
            typedef wobbly::SpringMesh::InstallResult IR;
            typedef std::unique_ptr <wobbly::SpringMesh::InstallResult> Handle;

            auto &fp (firstPreference);
            auto &sp (secondPreference);
            Handle first (new IR (springMesh.InstallAnchorSprings (install,
                                                                   fp,
                                                                   sp)));
            Handle second (new IR (springMesh.InstallAnchorSprings (install,
                                                                    fp,
                                                                    sp)));
            first.reset ();
            second.reset ();
            exit (0);
        }, ExitedWithCode (0), "");
    }

    TEST_F (SpringMesh, ForceBackToNeutralWhenHandlesExpireInNonReverseOrder)
    {
        /* All temporary, handles will expire in reverse order */
        {
            typedef wobbly::SpringMesh::InstallResult IR;
            typedef std::unique_ptr <wobbly::SpringMesh::InstallResult> Handle;

            auto &fp (firstPreference);
            auto &sp (secondPreference);

            /* Insert first temporary anchor between points (1) and (2) on
             * the grid. */
            wobbly::Point const firstInstall (EvenSize / 2, 0);
            Handle first (new IR (springMesh.InstallAnchorSprings (firstInstall,
                                                                   fp,
                                                                   sp)));

            /* Insert second temporary anchor between the first temporary
             * spring's anchor and base point (2) */
            wobbly::Point const secondInst (EvenSize / 2 +
                                                (TileWidth (EvenSize) / 4),
                                            0);
            Handle second (new IR (springMesh.InstallAnchorSprings (secondInst,
                                                                    fp,
                                                                    sp)));

            /* Move first handle to point above point (1) and second anchor
             * to point above point (2) */
            wobbly::Vector const firstMovement (-25, -50);
            wobbly::Vector const secondMovement (25, -50);

            wobbly::Point firstAnchorPoint (firstInstall);
            wgd::pointwise_add (firstAnchorPoint, firstMovement);
            ApplyMovement (first->data, firstMovement);

            wobbly::Point secondAnchorPoint (secondInst);
            wgd::pointwise_add (secondAnchorPoint, secondMovement);
            ApplyMovement (second->data, secondMovement);

            /* Kill the first handle before the second */
            first.reset ();
            second.reset ();
        }

        /* Calculate forces */
        auto result = springMesh.CalculateForces (SpringConstant);

        EXPECT_THAT (wobbly::PointView <double const> (result.forces, 1),
                     Eq (wobbly::Point (0, 0)));
        EXPECT_THAT (wobbly::PointView <double const> (result.forces, 2),
                     Eq (wobbly::Point (0, 0)));
    }

    /* This tests that if we pass in a different preference for a
     * desired distance for this spring, that force points in accordance
     * with that preference */
    TEST_F (SpringMesh, DesiredDistanceCanBeDifferentToSpringPositionAtGrab)
    {
        /* Insert a temporary anchor between points (1) and (2) on the grid */
        wobbly::Point const install (EvenSize / 2, 0);
        wobbly::Point const movement (25, -50);
        wobbly::Vector const desiredOffset (25, 0);

        std::array <double, 4> points;

        typedef wobbly::PointView <double const> DCPV;
        typedef DCPV const & (wobbly::Spring::*Get) () const;

        auto const prefOffset =
            [this, &desiredOffset, &points](wobbly::Spring const &spring,
                                            Get                  get,
                                            size_t               offset) {
                wobbly::PointView <double> pv (points, offset);
                wgd::assign (pv, (spring.*get) ());
                wgd::pointwise_add (pv, desiredOffset);

                return wobbly::PointView <double const> (points, offset);
            };

        using namespace std::placeholders;

        wobbly::SpringMesh::PosPreference firstPref =
            std::bind (prefOffset, _1, &wobbly::Spring::FirstPosition, 0);

        wobbly::SpringMesh::PosPreference secondPref =
            std::bind (prefOffset, _1, &wobbly::Spring::SecondPosition, 1);

        auto handle (springMesh.InstallAnchorSprings (install,
                                                      firstPref,
                                                      secondPref));

        ApplyMovement (handle.data, movement);

        auto result = springMesh.CalculateForces (SpringConstant);

        wobbly::Point anchorPosition (install);
        wgd::pointwise_add (anchorPosition, movement);

        wobbly::Vector const leftOffset (-TileWidth (EvenSize) / 2, 0);
        wobbly::Vector const rightOffset (TileWidth (EvenSize) / 2, 0);

        wobbly::Point anchorLeftPoint (anchorPosition);
        wgd::pointwise_add (anchorLeftPoint, leftOffset);
        wgd::pointwise_add (anchorLeftPoint, desiredOffset);

        wobbly::Point anchorRightPoint (anchorPosition);
        wgd::pointwise_add (anchorRightPoint, rightOffset);
        wgd::pointwise_add (anchorRightPoint, desiredOffset);

        /* We don't add anything to firstPoint and secondPoint here
         * because there will be an offset of desiredOffset from
         * the mesh already in the other direction, eg
         *
         * (newPosition - oldPosition) - delta.
         *
         * If we add delta to these points (eg, oldPosition) then
         * this will not model the equation correctly */
        wobbly::PointView <double const> firstPoint (mesh, 1);
        wobbly::PointView <double const> secondPoint (mesh, 2);

        EXPECT_THAT (wobbly::PointView <double const> (result.forces, 1),
                     PointsInSameDirection (firstPoint, anchorLeftPoint));
        EXPECT_THAT (wobbly::PointView <double const> (result.forces, 2),
                     PointsInSameDirection (secondPoint, anchorRightPoint));
    }

    constexpr double TextureWidth = 50.0f;
    constexpr double TextureHeight = 100.0f;
    wobbly::Point const TextureCenter = wobbly::Point (TextureWidth / 2,
                                                       TextureHeight / 2);


    class SpringBezierModel :
        public ::testing::Test
    {
        public:

            SpringBezierModel () :
                model (wobbly::Vector (0, 0),
                       TextureWidth,
                       TextureHeight)
            {
            }

        protected:

            wobbly::Model model;
    };

    template <typename Point>
    void PointCeiling (Point &p)
    {
        wgd::for_each_coordinate (p, [](auto const &coord) -> decltype(auto) {
            return std::ceil (coord);
        });
    }

    void MoveModelASmallAmount (wobbly::Model &model)
    {
        model.MoveModelTo (wobbly::Vector (1, 1));
        model.Step (1);
    }

    wobbly::Point GetTruncatedDeformedCenter (wobbly::Model const &model)
    {
        auto center (wobbly::Point (0.5, 0.5));
        auto point (model.DeformTexcoords (center));

        /* Not quite accurate, but truncate the returned point
         * so that we can do a reliable comparison */
        PointCeiling (point);
        return point;
    }

    TEST_F (SpringBezierModel, NoDeformationOnMovementWithNoAnchor)
    {
        MoveModelASmallAmount (model);
        auto point (GetTruncatedDeformedCenter (model));

        auto TextureCenterOffsetByOne (TextureCenter);
        wgd::pointwise_add (TextureCenterOffsetByOne, wobbly::Vector (1, 1));

        EXPECT_THAT (point,
                     Eq (TextureCenterOffsetByOne));
    }

    TEST_F (SpringBezierModel, NoDeformationOnMovementWithAnchorUngrabbed)
    {
        /* Anchor implicitly released at end of scope */
        {
            model.GrabAnchor (wobbly::Point (TextureWidth / 2, 0));
        }

        MoveModelASmallAmount (model);
        auto point (GetTruncatedDeformedCenter (model));
        auto TextureCenterOffsetByOne (TextureCenter);
        wgd::pointwise_add (TextureCenterOffsetByOne, wobbly::Vector (1, 1));


        EXPECT_THAT (point,
                     Eq (TextureCenterOffsetByOne));
    }

    TEST_F (SpringBezierModel, MovingEntireModelCausesNoDeformationWithAnchor)
    {
        auto anchor (model.GrabAnchor (wobbly::Point (TextureWidth / 2, 0)));

        MoveModelASmallAmount (model);
        auto point (GetTruncatedDeformedCenter (model));
        auto TextureCenterOffsetByOne (TextureCenter);
        wgd::pointwise_add (TextureCenterOffsetByOne, wobbly::Vector (1, 1));


        EXPECT_THAT (point,
                     Eq (TextureCenterOffsetByOne));
    }

    TEST_F (SpringBezierModel, MovingEntireModelChangesExtremesPositionsExactly)
    {
        unsigned int const x1 = 1;
        unsigned int const y1 = 1;
        unsigned int const x2 = TextureWidth + x1;
        unsigned int const y2 = TextureHeight + y1;

        model.MoveModelTo (wobbly::Point (x1, y1));

        std::array <wobbly::Point, 4> const extremes = model.Extremes ();
        Matcher <wobbly::Point const &> const textureEdges[] =
        {
            Eq (wobbly::Point (x1, y1)),
            Eq (wobbly::Point (x2, y1)),
            Eq (wobbly::Point (x1, y2)),
            Eq (wobbly::Point (x2, y2))
        };

        EXPECT_THAT (extremes, ElementsAreArray (textureEdges));
    }

    TEST_F (SpringBezierModel, MovingAnchorCausesDeformation)
    {
        auto anchor (model.GrabAnchor (wobbly::Point (TextureWidth / 2, 0)));

        anchor.MoveBy (wobbly::Vector (1, 1));
        auto point (GetTruncatedDeformedCenter (model));

        EXPECT_THAT (point,
                     Not (Eq (TextureCenter)));
    }

    TEST_F (SpringBezierModel, MovingAnchorWithSecondGrabCausesDeformation)
    {
        auto anchor (model.GrabAnchor (wobbly::Point (TextureWidth / 2, 0)));

        {
            auto secondAnchor (model.GrabAnchor (wobbly::Point (TextureWidth, 0)));
            anchor.MoveBy (wobbly::Vector (1, 1));
        }

        auto point (GetTruncatedDeformedCenter (model));

        EXPECT_THAT (point,
                     Not (Eq (TextureCenter)));
    }

    typedef std::tuple <wobbly::Point, wobbly::Point, wobbly::Point, size_t> SpringGrabParams;

    class SpringBezierModelGrabPositions :
        public SpringBezierModel,
        public WithParamInterface <SpringGrabParams>
    {
        public:

            SpringBezierModelGrabPositions () :
                grabPosition (std::get <0> (GetParam ())),
                oppositeGrabPosition (std::get <1> (GetParam ())),
                movement (std::get <2> (GetParam ())),
                extremeIndex (std::get <3> (GetParam ()))
            {
            }

            wobbly::Point const &grabPosition;
            wobbly::Point const &oppositeGrabPosition;
            wobbly::Point const &movement;
            size_t              extremeIndex;
    };

    typedef wobbly::Box <wobbly::Point> PointBox;

    /* Only tests the GrabIndex strategy */
    TEST_P (SpringBezierModelGrabPositions, GrabsCorrectIndex)
    {
        wobbly::Anchor grab (model.GrabAnchor (grabPosition));
        grab.MoveBy (movement);

        wobbly::Point transformed (grabPosition);
        wgd::pointwise_add (transformed, movement);

        EXPECT_THAT (model.Extremes ()[extremeIndex],
                     Eq (transformed));
    }

    TEST_P (SpringBezierModelGrabPositions, SettlesAtCurrentlyAnchoredPosition)
    {
        /* Check that when we grab the model from each of the four corners
         * and move that anchor by 100, 100 that the model always settles
         * at exactly 100, 100
         *
         * While exact positioning isn't possible without anchors grabbed,
         * it is almost always desired in this case */
        wobbly::Anchor grab (model.GrabAnchor (grabPosition));
        grab.MoveBy (wobbly::Vector (100, 100));

        /* Wait for model to settle */
        while (model.Step (1));

        EXPECT_THAT (model.Extremes ()[0],
                     Eq (wobbly::Point (100, 100)));
    }

    TEST_P (SpringBezierModelGrabPositions, SettlesAfterReleasingSecond)
    {
        /* Check that when we grab the model from each of the four corners
         * and at each corresponding opposite corner, move the first anchor
         * by 100, 100, then release the second anchor, that the model
         * always settles at exactly 100, 100
         *
         * While exact positioning isn't possible without anchors grabbed,
         * it is almost always desired in this case */
        wobbly::Anchor grab (model.GrabAnchor (grabPosition));

        {
            wobbly::Anchor secondGrab (model.GrabAnchor (oppositeGrabPosition));
            grab.MoveBy (wobbly::Vector (100, 100));
        }

        /* Wait for model to settle */
        while (model.Step (1));

        /* We can't be exact here, since a full integration is required to
         * compute the target position. */
        EXPECT_THAT (model.Extremes ()[0],
                     WithinGeometry (PointBox (wobbly::Point (97.0, 97.0),
                                               wobbly::Point (103.0, 103.0))));
    }

    SpringGrabParams const springGrabParams[] =
    {
        SpringGrabParams (wobbly::Point (0.0, 0.0),
                          wobbly::Point (TextureWidth, TextureHeight),
                          wobbly::Point (-1.0, -1.0),
                          0),
        SpringGrabParams (wobbly::Point (TextureWidth, 0.0),
                          wobbly::Point (0.0, TextureHeight),
                          wobbly::Point (1.0, -1.0),
                          1),
        SpringGrabParams (wobbly::Point (0.0, TextureHeight),
                          wobbly::Point (TextureWidth, 0.0),
                          wobbly::Point (-1.0, 1.0),
                          2),
        SpringGrabParams (wobbly::Point (TextureWidth, TextureHeight),
                          wobbly::Point (0.0, 0.0),
                          wobbly::Point (1.0, 1.0),
                          3)
    };

    INSTANTIATE_TEST_CASE_P (Extremes, SpringBezierModelGrabPositions,
                             ValuesIn (springGrabParams));
                             

    double const ModelScaleFactorX = 2.0f;
    double const ModelScaleFactorY = 3.0f;

    double const TextureWidthAfterResize = ModelScaleFactorX * TextureWidth;
    double const TextureHeightAfterResize = ModelScaleFactorY * TextureHeight;

    typedef Matcher <wobbly::Point const &> PointMatcher;

    TEST_F (SpringBezierModel, PositionsScaledAfterResize)
    {
        wobbly::Vector const scaleFactor (ModelScaleFactorX,
                                          ModelScaleFactorY);

        std::array <wobbly::Point, 4> const extremes = model.Extremes ();

        /* Older versions of gmock don't support matching against a vector */
        auto scaledPointMatcher =
            [&scaleFactor](wobbly::Point p) -> PointMatcher {
                wgd::pointwise_scale (p, scaleFactor);
                return Eq (p);
            };

        PointMatcher const scaledExtremes[4] =
        {
            scaledPointMatcher (extremes[0]),
            scaledPointMatcher (extremes[1]),
            scaledPointMatcher (extremes[2]),
            scaledPointMatcher (extremes[3])
        };

        model.ResizeModel (TextureWidthAfterResize,
                           TextureHeightAfterResize);

        EXPECT_THAT (model.Extremes (), ElementsAreArray (scaledExtremes));
    }

    TEST_F (SpringBezierModel, PositionsScaledRelativeToModelOrigin)
    {
        wobbly::Vector const scaleFactor (ModelScaleFactorX,
                                          ModelScaleFactorY);
        wobbly::Vector const movement (10.0f, 10.0f);

        model.MoveModelTo (movement);

        std::array <wobbly::Point, 4> const extremes = model.Extremes ();
        auto scaledPointMatcher =
            [&scaleFactor, &movement](wobbly::Point p) -> PointMatcher {
                wgd::pointwise_subtract (p, movement);
                wgd::pointwise_scale (p, scaleFactor);
                wgd::pointwise_add (p, movement);
                return Eq (p);
            };

        PointMatcher const scaledExtremes[4] =
        {
            scaledPointMatcher (extremes[0]),
            scaledPointMatcher (extremes[1]),
            scaledPointMatcher (extremes[2]),
            scaledPointMatcher (extremes[3])
        };

        model.ResizeModel (TextureWidthAfterResize,
                           TextureHeightAfterResize);

        EXPECT_THAT (model.Extremes (), ElementsAreArray (scaledExtremes));
    }

    TEST_F (SpringBezierModel, NetForceIsZeroAfterResizingSettledModel)
    {
        model.ResizeModel (TextureWidthAfterResize,
                           TextureHeightAfterResize);

        EXPECT_FALSE (model.Step (1));
    }

    TEST_F (SpringBezierModel, NetForceIsZeroAfterResizingGrabbedSettledModel)
    {
        /* We resize to something absurd as we want to ensure that we don't
         * get constrained to the old model size' constraint extents after
         * resizing while grabbed */
        wobbly::Anchor grab (model.GrabAnchor (model.Extremes ()[0]));
        model.ResizeModel (TextureWidthAfterResize * 100,
                           TextureHeightAfterResize * 100);
        EXPECT_FALSE (model.Step (1));
    }

    TEST_F (SpringBezierModel, NetForceIsZeroAfterMovingGrabbedSettledModel)
    {
        /* We resize to something absurd as we want to ensure that we don't
         * get constrained to the old model size' constraint extents after
         * resizing while grabbed */
        wobbly::Anchor grab (model.GrabAnchor (model.Extremes ()[0]));
        wobbly::Vector translation (1000, 1000);
        
        model.MoveModelBy (translation);
        /* Just moving the model, not the anchor - all points and targets
         * should move */
        EXPECT_FALSE (model.Step (1));
    }

    TEST_F (SpringBezierModel, PositionIsTopLeftCornerAtSettled)
    {
        wobbly::Vector const position (100, 100);
        model.MoveModelBy (position);

        /* We can assume that Extremes ()[0] is the top-left position as
         * the other tests enforce it being the minimum,minimum position */
        EXPECT_THAT (model.Extremes ()[0], Eq (position));
    }

    /* Tests for both the InstallPoint and GrabAnchor strategies */
    template <typename AnchorStrategyFactory>
    class SpringBezierModelAnchorStrategy :
        public SpringBezierModel
    {
        public:

            AnchorStrategyFactory createAnchorFor;
    };

    /* GrabAnchorStrategy grab a single point on the mesh and move it */
    struct GrabAnchorStrategyFactory
    {
        wobbly::Anchor operator () (wobbly::Model       &model,
                                    wobbly::Point const &grabPoint)
        {
            return model.GrabAnchor (grabPoint);
        }
    };

    /* InstallAnchorStrategy installs a new anchor on the mesh */
    struct InstallAnchorStrategyFactory
    {
        wobbly::Anchor operator () (wobbly::Model       &model,
                                    wobbly::Point const &grabPoint)
        {
            return model.InsertAnchor (grabPoint);
        }
    };

    typedef ::testing::Types <GrabAnchorStrategyFactory,
                              InstallAnchorStrategyFactory> AnchorStrategyTypes;

    TYPED_TEST_CASE (SpringBezierModelAnchorStrategy, AnchorStrategyTypes);

    /* We can verify this by grabbing an anchor at a known position
     * and then resizing the model. The model should not have net force
     * and the anchors should have moved along with the model */
    TYPED_TEST (SpringBezierModelAnchorStrategy, AnchorMovedAfterMeshResize)
    {
        wobbly::Vector const grabPoint (TextureWidth,
                                        TextureHeight);
        wobbly::Anchor grab (this->createAnchorFor (this->model, grabPoint));

        this->model.ResizeModel (TextureWidthAfterResize,
                                 TextureHeightAfterResize);

        EXPECT_FALSE (this->model.Step (1));
    }
    
    TYPED_TEST (SpringBezierModelAnchorStrategy, AnchorMovedAfterMeshMove)
    {
        wobbly::Vector const grabPoint (TextureWidth,
                                        TextureHeight);
        wobbly::Anchor grab (this->createAnchorFor (this->model, grabPoint));

        this->model.MoveModelBy (wobbly::Vector (100, 100));

        EXPECT_FALSE (this->model.Step (1));
    }

    TYPED_TEST (SpringBezierModelAnchorStrategy, EntireModelMovesWhileGrabbed)
    {
        /* Create an anchor at the midpoint, move it by 100, 100 and then move
         * the entire model backwards by 100, 100. The result should be that
         * the model's topleft most extreme will be around 0, 0. We will move
         * the entire model backwards and anchor by one step along the way and
         * integrate each  time, ensuring that integration has no effect
         * on movement.
         *
         * Because the model is automatically snapped back to its target
         * position as soon as integration is complete, we'll need to do the
         * same on two models and calculate how many integrations are required
         * to get the model to settle. We'll apply n - 1 integrations to the
         * second and then test it.
         */
        wobbly::Vector const grabPoint (TextureWidth / 2, TextureHeight / 2);

        wobbly::Model referenceModel (wobbly::Vector (0, 0),
                                      TextureWidth,
                                      TextureHeight);

        wobbly::Anchor grab (this->createAnchorFor (this->model, grabPoint));
        wobbly::Anchor referenceGrab (this->createAnchorFor (referenceModel,
                                                             grabPoint));

        size_t const distance = 100;
        
        for (size_t i = 0; i < distance; ++i)
        {
            float const positive = 1;
            float const negative = static_cast <float> (positive) * -1.0;

            grab.MoveBy (wobbly::Vector (positive, positive));
            this->model.MoveModelBy (wobbly::Vector (negative, negative));
            while (this->model.Step (1));
            
            referenceGrab.MoveBy (wobbly::Vector (positive, positive));
            referenceModel.MoveModelBy (wobbly::Vector (negative, negative));
            while (referenceModel.Step (1));
        }
        
        /* Complete steps on reference model, but track how long it takes */
        int requiredStepsToSettle = 0;
        
        while (referenceModel.Step (1))
            ++requiredStepsToSettle;

        /* Step test-against model for requiredStepsToSettle - 1 */
        requiredStepsToSettle = std::max (requiredStepsToSettle - 1, 0);
        while (requiredStepsToSettle--)
            this->model.Step (1);

        /* Slightly higher range */
        EXPECT_THAT (this->model.Extremes ()[0],
                     WithinGeometry (PointBox (wobbly::Point (-2.0, -2.0),
                                               wobbly::Point (2.0, 2.0))));
    }

    /* The only way we can test this is to perform operations dependent
     * on a target position and ensure that they are precise to the grab's
     * position */
    TYPED_TEST (SpringBezierModelAnchorStrategy, TargetWithinRangeGrabbed)
    {
        /* Create an anchor on 0, 0 and then move it to 100, 100, then move
         * it back to 0, 0. The end result should be that the model position
         * will end up back at 0, 0. We can't observe the target positions
         * so we need to do it this way */

        wobbly::Vector const grabPoint (0, 0);
        wobbly::Anchor grab (this->createAnchorFor (this->model, grabPoint));

        grab.MoveBy (wobbly::Point (100, 100));
        this->model.MoveModelTo (wobbly::Point (0, 0));

        /* Wait until the model has completely settled */
        while (this->model.Step (1));

        EXPECT_THAT (this->model.Extremes ()[0],
                     WithinGeometry (PointBox (wobbly::Point (-1.5, -1.5),
                                               wobbly::Point (1.5, 1.5))));
    }
    
    TYPED_TEST (SpringBezierModelAnchorStrategy, ConsistentMovementManyGrabs)
    {
        /* Create an anchor on 0, 0 and then move it to 100, 100, then move
         * it back to 0, 0. The end result should be that the model position
         * will end up back at 0, 0. We can't observe the target positions
         * so we need to do it this way */
        for (size_t i = 0; i < 5; ++i)
        {
            this->model.MoveModelTo (wobbly::Point (0, 0));
            wobbly::Vector const grabPoint (0, 0);
            wobbly::Anchor grab (this->createAnchorFor (this->model,
                                                        grabPoint));

            grab.MoveBy (wobbly::Point (100, 100));
            this->model.MoveModelTo (wobbly::Point (0, 0));

            /* Wait until the model has completely settled */
            while (this->model.Step (1));
        }

        EXPECT_THAT (this->model.Extremes ()[0],
                     WithinGeometry (PointBox (wobbly::Point (-1.5, -1.5),
                                               wobbly::Point (1.5, 1.5))));
    }

    /* The only way we can test this is to perform operations dependent
     * on a target position and ensure that they are precise to the grab's
     * position */
    TYPED_TEST (SpringBezierModelAnchorStrategy, AnchorsChangesDontAffectTarget)
    {
        /* Create an anchor on 0, 0, then at TextureWidth, 0 and move it by
         * 100, 100, then move the model it back to 0, 0. This checks if
         * the TargetPosition machinery is able to handle different anchor
         * grabs */

        {
            wobbly::Vector const grabPoint (0, 0);
            wobbly::Anchor grab (this->createAnchorFor (this->model,
                                                        grabPoint));
        }

        wobbly::Vector const grabPoint (TextureWidth, 0);
        wobbly::Anchor grab (this->createAnchorFor (this->model,
                                                    grabPoint));

        grab.MoveBy (wobbly::Point (100, 100));
        this->model.MoveModelTo (wobbly::Point (0, 0));

        /* Wait until the model has completely settled */
        while (this->model.Step (1));

        EXPECT_THAT (this->model.Extremes ()[0],
                     WithinGeometry (PointBox (wobbly::Point (-1.5, -1.5),
                                               wobbly::Point (1.5, 1.5))));
    }

    TYPED_TEST (SpringBezierModelAnchorStrategy, TargetRemainsAfterRelease)
    {
        /* This time integrate the model for a short period while grabbed
         * and then move it to a new position. This should still cause its
         * target position to end up roughly in the same place */
        {
            wobbly::Vector const grabPoint (0, 0);
            wobbly::Anchor grab (this->createAnchorFor (this->model,
                                                        grabPoint));

            grab.MoveBy (wobbly::Point (100, 100));
            this->model.Step (2);
        }

        this->model.MoveModelTo (wobbly::Point (0, 0));

        /* Wait until the model has completely settled */
        while (this->model.Step (1));

        EXPECT_THAT (this->model.Extremes ()[0],
                     WithinGeometry (PointBox (wobbly::Point (-1.5, -1.5),
                                               wobbly::Point (1.5, 1.5))));
    }

    TYPED_TEST (SpringBezierModelAnchorStrategy, MoreAnchoredModelNeverSettles)
    {
        wobbly::Model oneAnchorModel (wobbly::Vector (0, 0),
                                      TextureWidth,
                                      TextureHeight);

        wobbly::Point const firstGrabPoint (0, 0);
        wobbly::Point const secondGrabPoint (TextureWidth, 0);

        auto firstForOneAnchorModel (this->createAnchorFor (oneAnchorModel,
                                                            firstGrabPoint));
        auto firstForTwoAnchorModel (this->createAnchorFor (this->model,
                                                            firstGrabPoint));
        auto secondForTwoAnchorModel (this->createAnchorFor (this->model,
                                                             secondGrabPoint));

        wobbly::Vector const firstMovement (-100, 0);

        firstForOneAnchorModel.MoveBy (firstMovement);
        firstForTwoAnchorModel.MoveBy (firstMovement);

        /* Keep integrating both models until one of them is settled. The
         * result should be that the first model finishes while the second
         * one does not */
        bool continueFirstModel = true;
        bool continueSecondModel = true;

        while (continueFirstModel && continueSecondModel)
        {
            continueFirstModel = oneAnchorModel.Step (1);
            continueSecondModel = this->model.Step (1);
        }

        EXPECT_FALSE (continueFirstModel);
        EXPECT_TRUE (continueSecondModel);

    }

    TYPED_TEST (SpringBezierModelAnchorStrategy, ForcesExistAfterMovingAnchor)
    {
        /* Create an anchor and move it. Step (0) should return true */
        wobbly::Vector const grabPoint (0, 0);
        wobbly::Anchor grab (this->createAnchorFor (this->model,
                                                    grabPoint));

        grab.MoveBy (wobbly::Point (100, 100));
        EXPECT_TRUE (this->model.Step (0));
    }

    TYPED_TEST (SpringBezierModelAnchorStrategy, ForcesExistAfterReleaseAnchor)
    {
        {
            /* Create an anchor and move it. Step (0) should return true */
            wobbly::Vector const grabPoint (0, 0);
            wobbly::Anchor grab (this->createAnchorFor (this->model,
                                                        grabPoint));

            grab.MoveBy (wobbly::Point (100, 100));

            /* Step the model once, this will make the model unequal */
            this->model.Step (1);

            /* Grab goes away here but the model is still unequal */
        }

        EXPECT_TRUE (this->model.Step (0));
    }

    void GrabModelMoveAndStepASmallAmount (wobbly::Model &model)
    {
        wobbly::Vector const grabPoint (model.Extremes ()[3]);
        wobbly::Anchor anchor (model.GrabAnchor (grabPoint));

        anchor.MoveBy (wobbly::Point (100, 100));

        /* Twenty steps is reasonable */
        for (int i = 0; i < 20; ++i)
            model.Step (16);
    }

    TEST (SpringBezierModelSettings, ModelWithHigherSpringTakesFasterFirstStep)
    {
        /* We want to create two models each with different spring constants
         * and check that the second one moves faster than the first after
         * taking the first step.
         *
         * We'll test this by reading the top-left hand point and grabbing
         * on the bottom left. The model that moves quicker should have its
         * top-left hand point also move a lot quicker
         */

        wobbly::Model::Settings lowerK = wobbly::Model::DefaultSettings;
        wobbly::Model::Settings higherK = wobbly::Model::DefaultSettings;

        lowerK.springConstant -= 2.0f;

        wobbly::Model lowerSpringKModel (wobbly::Vector (0, 0),
                                         TextureWidth,
                                         TextureHeight,
                                         lowerK);
        wobbly::Model higherSpringKModel (wobbly::Vector (0, 0),
                                          TextureWidth,
                                          TextureHeight,
                                          higherK);

        GrabModelMoveAndStepASmallAmount (lowerSpringKModel);
        GrabModelMoveAndStepASmallAmount (higherSpringKModel);

        EXPECT_GT (wgd::get <0> (higherSpringKModel.Extremes ()[0]),
                   wgd::get <0> (lowerSpringKModel.Extremes ()[0]));
    }

    TEST (SpringBezierModelSettings, ModelWithLowerFrictionTakesFasterFirstStep)
    {
        wobbly::Model::Settings lowerF = wobbly::Model::DefaultSettings;
        wobbly::Model::Settings higherF = wobbly::Model::DefaultSettings;

        lowerF.friction -= 2.0f;

        wobbly::Model lowerFrictionModel (wobbly::Vector (0, 0),
                                          TextureWidth,
                                          TextureHeight,
                                          lowerF);
        wobbly::Model higherFrictionModel (wobbly::Vector (0, 0),
                                           TextureWidth,
                                           TextureHeight,
                                           higherF);

        GrabModelMoveAndStepASmallAmount (lowerFrictionModel);
        GrabModelMoveAndStepASmallAmount (higherFrictionModel);

        EXPECT_GT (wgd::get <0> (lowerFrictionModel.Extremes ()[0]),
                   wgd::get <0> (higherFrictionModel.Extremes ()[0]));
    }

    struct MockIntegration
    {
        MockIntegration ()
        {
            EXPECT_CALL (*this, Reset (_)).Times (AtLeast (0));
            EXPECT_CALL (*this, Step (_, _, _, _, _, _)).Times (AtLeast (0));

            ON_CALL (*this, Step (_, _, _, _, _, _))
                .WillByDefault (Return (true));
        }

        MOCK_METHOD1 (Reset, void (size_t));
        MOCK_METHOD6 (Step, bool (size_t,
                                  double,
                                  double,
                                  double,
                                  wobbly::MeshArray       &,
                                  wobbly::MeshArray const &));

        wobbly::MeshArray & Velocities ()
        {
            static wobbly::MeshArray array;
            return array;
        }
    };

    class AnchoredIntegrationLoop :
        public ::testing::Test
    {
        public:

            AnchoredIntegrationLoop () :
                integrator (strategy)
            {
                positions.fill (0);
                forces.fill (0);
            }

            MockIntegration strategy;
            wobbly::AnchoredIntegration <MockIntegration> integrator;

            wobbly::MeshArray positions;
            wobbly::MeshArray forces;
            wobbly::AnchorArray anchors;
    };

    TEST_F (AnchoredIntegrationLoop, ResetIndicesWithAnchor)
    {
        EXPECT_CALL (strategy, Reset (0)).Times (1);
        anchors.Lock (0);

        integrator (positions, forces, anchors, 0.0);
    }

    TEST_F (AnchoredIntegrationLoop, StepUnanchoredPoints)
    {
        EXPECT_CALL (strategy, Step (0, _, _, _, _, _)).Times (1);

        integrator (positions, forces, anchors, 0.0);
    }

    template <typename Integrator>
    class IntegrationStrategy :
        public Test
    {
        public:

            IntegrationStrategy ()
            {
                points.fill (0.0);
                forces.fill (0.0);
            }

            Integrator integrator;
            wobbly::MeshArray points;
            wobbly::MeshArray forces;
    };

    typedef Types <wobbly::EulerIntegration> IntegrationStrategies;
    TYPED_TEST_CASE (IntegrationStrategy, IntegrationStrategies);

    TYPED_TEST (IntegrationStrategy, NoMotionOnReset)
    {
        /* Call the reset () function on the integrator. No changes
         * should occurr on the position at that index */
        wobbly::PointView <double> pointView (TestFixture::points, 0);

        TestFixture::integrator.Reset (0);

        EXPECT_THAT (pointView, Eq (wobbly::Point (0, 0)));
    }

    TYPED_TEST (IntegrationStrategy, EffectiveVelocityChangedToZeroOnReset)
    {
        /* Apply a force once to a frictionless object and integrate it.
         * Call reset and integrate again without any force. The result is
         * no change in position as the velocity was reset */
        wobbly::PointView <double> forceView (TestFixture::forces, 0);
        wobbly::PointView <double> pointView (TestFixture::points, 0);

        /* First apply a force to an object and integrate */
        wgd::set <0> (forceView, 1.0);
        TestFixture::integrator.Step (0,
                                      1.0,
                                      1.0,
                                      1.0,
                                      TestFixture::points,
                                      TestFixture::forces);

        wobbly::Point expectedPosition;
        wgd::assign (expectedPosition, pointView);

        /* Remove force, reset and integrate again */
        wgd::set <0> (forceView, 0.0);
        TestFixture::integrator.Reset (0);
        TestFixture::integrator.Step (0,
                                      1.0,
                                      1.0,
                                      1.0,
                                      TestFixture::points,
                                      TestFixture::forces);

        /* After integration, the point should not have moved because
         * it has no velocity */
        EXPECT_THAT (pointView, Eq (expectedPosition));
    }

    TYPED_TEST (IntegrationStrategy, VelocityAffectedWithNewForcesAfterReset)
    {
        wobbly::PointView <double> forceView (TestFixture::forces, 0);
        wobbly::PointView <double> pointView (TestFixture::points, 0);

        wobbly::Point initialPosition;
        wgd::assign (initialPosition, pointView);

        /* Reset, apply force and integrate */
        wgd::set <0> (forceView, 1.0);
        TestFixture::integrator.Reset (0);
        TestFixture::integrator.Step (0,
                                      1.0,
                                      1.0,
                                      1.0,
                                      TestFixture::points,
                                      TestFixture::forces);

        EXPECT_THAT (pointView,
                     Not (Eq (initialPosition)));
    }

    TYPED_TEST (IntegrationStrategy, PositionChangesParabolicallyOverTime)
    {
        wobbly::PointView <double> forceView (TestFixture::forces, 0);
        wobbly::PointView <double> pointView (TestFixture::points, 0);

        std::function <double (int)> frictionlessHorizontalPositionFunction =
            [this, &pointView, &forceView](int timestep) -> double {
                TypeParam integrator;

                /* Reset velocity and force */
                wgd::assign (pointView, wobbly::Point (0, 0));
                wgd::assign (forceView, wobbly::Vector (1.0f, 0));

                integrator.Step (0,
                                 timestep,
                                 1.0,
                                 1.0,
                                 TestFixture::points,
                                 TestFixture::forces);

                return wgd::get <0> (pointView);
            };

        EXPECT_THAT (frictionlessHorizontalPositionFunction,
                     SatisfiesModel (Parabolic <double> ()));
    }

    TEST (SpringStep, ContinueStepWhenSpringsHaveForces)
    {
        /* All points will start at zero, so a positive spring force
         * will already be exerted */
        wobbly::MeshArray positions;
        wobbly::AnchorArray anchors;
        double const springConstant = 1.0;
        double const springFriction = 1.0;
        wobbly::Vector const springDimensions (10.0, 10.0);

        positions.fill (0.0);

        MockIntegration                      integrator;
        wobbly::SpringStep <MockIntegration> stepper (integrator,
                                                      positions,
                                                      springConstant,
                                                      springFriction,
                                                      springDimensions);

        EXPECT_TRUE (stepper (positions, anchors));
    }
}
