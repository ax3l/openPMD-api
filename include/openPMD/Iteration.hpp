/* Copyright 2017-2020 Fabian Koller
 *
 * This file is part of openPMD-api.
 *
 * openPMD-api is free software: you can redistribute it and/or modify
 * it under the terms of of either the GNU General Public License or
 * the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * openPMD-api is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License and the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * and the GNU Lesser General Public License along with openPMD-api.
 * If not, see <http://www.gnu.org/licenses/>.
 */
#pragma once

#include "openPMD/backend/Attributable.hpp"
#include "openPMD/backend/Container.hpp"
#include "openPMD/Mesh.hpp"
#include "openPMD/ParticleSpecies.hpp"


namespace openPMD
{
/** @brief  Logical compilation of data from one snapshot (e.g. a single simulation cycle).
 *
 * @see https://github.com/openPMD/openPMD-standard/blob/latest/STANDARD.md#required-attributes-for-the-basepath
 */
class Iteration : public Attributable
{
    template<
            typename T,
            typename T_key,
            typename T_container
    >
    friend class Container;
    friend class Series;

public:
    Iteration(Iteration const&);
    Iteration& operator=(Iteration const&);

    /**
     * @tparam  T   Floating point type of user-selected precision (e.g. float, double).
     * @return  Global reference time for this iteration.
     */
    template< typename T >
    T time() const;
    /** Set the global reference time for this iteration.
     *
     * @tparam  T       Floating point type of user-selected precision (e.g. float, double).
     * @param   newTime Global reference time for this iteration.
     * @return  Reference to modified iteration.
     */
    template< typename T >
    Iteration& setTime(T newTime);

    /**
     * @tparam  T   Floating point type of user-selected precision (e.g. float, double).
     * @return  Time step used to reach this iteration.
     */
    template< typename T >
    T dt() const;
    /** Set the time step used to reach this iteration.
     *
     * @tparam  T     Floating point type of user-selected precision (e.g. float, double).
     * @param   newDt Time step used to reach this iteration.
     * @return  Reference to modified iteration.
     */
    template< typename T >
    Iteration& setDt(T newDt);

    /**
     * @return Conversion factor to convert time and dt to seconds.
     */
    double timeUnitSI() const;
    /** Set the conversion factor to convert time and dt to seconds.
     *
     * @param  newTimeUnitSI new value for timeUnitSI
     * @return Reference to modified iteration.
     */
    Iteration& setTimeUnitSI(double newTimeUnitSI);

    /** Close an iteration
     *
     * No further (backend-propagating) accesses may be performed on this
     * iteration. A closed iteration may not (yet) be reopened.
     *
     * With an MPI-parallel series, close is an MPI-collective operation.
     *
     * @return Reference to iteration.
     */
    /*
     * Note: If the API is changed in future to allow reopening closed
     * iterations, measures should be taken to prevent this in the streaming
     * API. Currently, disallowing to reopen closed iterations satisfies
     * the requirements of the streaming API.
     */
    Iteration &
    close( bool flush = true );

    /**
     * @brief Has the iteration been closed?
     *        A closed iteration may not (yet) be reopened.
     *
     * @return Whether the iteration has been closed.
     */
    bool
    closed() const;

    /**
     * @brief Has the iteration been closed by the writer?
     *        Background: Upon calling Iteration::close(), the openPMD API
     *        will add metadata to the iteration in form of an attribute,
     *        indicating that the iteration has indeed been closed.
     *        Useful mainly in streaming context when a reader inquires from
     *        a writer that it is done writing.
     *
     * @return Whether the iteration has been explicitly closed (yet) by the
     *         writer.
     */
    bool
    closedByWriter() const;

    Container< Mesh > meshes;
    Container< ParticleSpecies > particles; //particleSpecies?

    virtual ~Iteration() = default;
private:
    Iteration();

    void flushFileBased(std::string const&, uint64_t);
    void flushGroupBased(uint64_t);
    void flush();
    void read();

    /**
     * @brief Whether an iteration has been closed yet.
     *
     */
    enum class CloseStatus
    {
        Open,             //!< Iteration has not been closed
        ClosedInFrontend, /*!< Iteration has been closed, but task has not yet
                               been propagated to the backend */
        ClosedInBackend,  /*!< Iteration has been closed and task has been
                               propagated to the backend */
        ClosedTemporarily /*!< Iteration has been closed internally and may
                               be reopened later */
    };

    /*
     * An iteration may be logically closed in the frontend,
     * but not necessarily yet in the backend.
     * Will be propagated to the backend upon next flush.
     * Store the current status.
     */
    std::shared_ptr< CloseStatus > m_closed =
        std::make_shared< CloseStatus >( CloseStatus::Open );

    /*
     * @brief Check recursively whether this Iteration is dirty.
     *        It is dirty if any attribute or dataset is read from or written to
     *        the backend.
     *
     * @return true If dirty.
     * @return false Otherwise.
     */
    bool
    dirtyRecursive() const;

    virtual void linkHierarchy(std::shared_ptr< Writable > const& w);
};  // Iteration

extern template
float
Iteration::time< float >() const;

extern template
double
Iteration::time< double >() const;

extern template
long double
Iteration::time< long double >() const;

template< typename T >
inline T
Iteration::time() const
{ return Attributable::readFloatingpoint< T >("time"); }


extern template
float
Iteration::dt< float >() const;

extern template
double
Iteration::dt< double >() const;

extern template
long double
Iteration::dt< long double >() const;

template< typename T >
inline T
Iteration::dt() const
{ return Attributable::readFloatingpoint< T >("dt"); }
} // openPMD
