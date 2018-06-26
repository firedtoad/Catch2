/*
 *  Created by Phil Nash on 15/6/2018.
 *
 *  Distributed under the Boost Software License, Version 1.0. (See accompanying
 *  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
 */
#ifndef TWOBLUECUBES_CATCH_GENERATORS_HPP_INCLUDED
#define TWOBLUECUBES_CATCH_GENERATORS_HPP_INCLUDED

#include "catch_common.h"

#include <memory>
#include <vector>
#include <map>
#include <cassert>
#include <string>

#include "internal/catch_suppress_warnings.h"

namespace Catch {
namespace generators {

    namespace pf{
        template<typename T, typename... Args>
        std::unique_ptr<T> make_unique( Args&&... args ) {
            return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
        }
    }

    template<typename T>
    struct IGenerator {
        virtual ~IGenerator() {}
        virtual auto get( size_t index ) const -> T = 0;
    };

    template<typename T>
    class SingleValueGenerator : public IGenerator<T> {
        T m_value;
    public:
        SingleValueGenerator( T const& value ) : m_value( value ) {}

        auto get( size_t ) const -> T override {
            return m_value;
        }
    };

    template<typename T>
    class FixedValuesGenerator : public IGenerator<T> {
        std::vector<T> m_values;

    public:
        FixedValuesGenerator( std::initializer_list<T> values ) : m_values( values ) {}

        auto get( size_t index ) const -> T override {
            return m_values[index];
        }
    };

    template<typename T>
    class RangeGenerator : public IGenerator<T> {
        T const m_first;
        T const m_last;

    public:
        RangeGenerator( T const& first, T const& last ) : m_first( first ), m_last( last ) {
            assert( m_last > m_first );
        }

        auto get( size_t index ) const -> T override {
            return m_first+index;
        }
    };

    template<typename T>
    class Generator;

    template<typename T>
    auto value( T const& val ) -> Generator<T>;


    template<typename Left, typename Right>
    struct ComposedGeneratorsMustHaveTheSameUnderlyingType;

    class GeneratorBase {
    protected:
        size_t m_size = 0;

    public:
        GeneratorBase( size_t size ) : m_size( size ) {}
        virtual ~GeneratorBase();
        auto size() const -> size_t { return m_size; }
    };

    struct NullGenerator {};

    template<typename T>
    class Generator : public GeneratorBase {
        std::unique_ptr<IGenerator<T>> m_generator;
        std::vector<Generator<T>> m_generators;

        void add( Generator&& other ) {
            if( other.m_generator )
                m_generators.push_back( std::move( other ) );
            else {
                std::move( other.m_generators.begin(), other.m_generators.end(), std::back_inserter( m_generators ) );
            }
        }
    public:
        using type = T;

        Generator() : GeneratorBase( 0 ) {}

        Generator( size_t size, std::unique_ptr<IGenerator<T>> generator )
        :   GeneratorBase( size ),
            m_generator( std::move( generator ) )
        {}
        Generator( Generator&& g1, Generator&& g2 )
        : GeneratorBase( g1.size() + g2.size() ) {
            m_generators.reserve( m_size );
            add( std::move( g1 ) );
            add( std::move( g2 ) );
        }

        auto operator[]( size_t index ) const -> T {
            assert( index < m_size );
            if( m_generator )
                return m_generator->get( index );

            size_t sizes = 0;
            for( auto const& gen : m_generators ) {
                auto localIndex = index-sizes;
                sizes += gen.size();
                if( index < sizes )
                    return gen[localIndex];
            }
            assert(false); // should never happen
            throw std::logic_error("this should never happen");
        }
    };

    using GeneratorBasePtr = std::unique_ptr<GeneratorBase>;

    std::vector<size_t> randomiseIndices( size_t selectionSize, size_t sourceSize );

    template<typename T>
    class GeneratorRandomiser : public IGenerator<T> {
        Generator<T> m_baseGenerator;

        std::vector<size_t> m_indices;
    public:
        GeneratorRandomiser( Generator<T>&& baseGenerator, size_t numberOfItems )
        :   m_baseGenerator( std::move( baseGenerator ) ),
            m_indices( randomiseIndices( numberOfItems, m_baseGenerator.size() ) )
        {}

        auto get( size_t index ) const -> T override {
            return m_baseGenerator[m_indices[index]];
        }
    };

    template<typename T>
    struct RequiresASpecialisationFor;

    template<typename T>
    auto all() -> Generator<T> { return RequiresASpecialisationFor<T>(); }


    template<typename T>
    auto range( T const& first, T const& last ) -> Generator<T> {
        return Generator<T>( 1+(last-first), pf::make_unique<RangeGenerator<T>>( first, last ) );
    }
    template<>
    inline auto all<int>() -> Generator<int> {
        return range( std::numeric_limits<int>::min(), std::numeric_limits<int>::max()-1 );
    }


    template<typename T>
    auto random( T const& first, T const& last ) -> Generator<T> {
        auto gen = range( first, last );
        auto size = gen.size();

        return Generator<T>( size, pf::make_unique<GeneratorRandomiser<T>>( std::move( gen ), size ) );
    }
    template<typename T>
    auto random( size_t size ) -> Generator<T> {
        return Generator<T>( size, pf::make_unique<GeneratorRandomiser<T>>( all<T>(), size ) );
    }

    template<typename T>
    auto values( std::initializer_list<T> values ) -> Generator<T> {
        return Generator<T>( values.size(), pf::make_unique<FixedValuesGenerator<T>>( values ) );
    }
    template<typename T>
    auto value( T const& val ) -> Generator<T> {
        return Generator<T>( 1, pf::make_unique<SingleValueGenerator<T>>( val ) );
    }

    template<typename... Ts>
    auto table( std::initializer_list<std::tuple<Ts...>>&& tuples ) -> Generator<std::tuple<Ts...>> {
        return values<std::tuple<Ts...>>( std::forward<std::initializer_list<std::tuple<Ts...>>>( tuples ) );
    }



    template<typename T>
    auto operator << ( NullGenerator, T const& val ) -> Generator<T> {
        return value( val );
    }
    template<typename T>
    auto operator << ( NullGenerator, Generator<T>&& generator ) -> Generator<T> {
        return std::move(generator);
    }
    template<typename T>
    auto operator << ( Generator<T>&& g1, Generator<T>&& g2 ) -> Generator<T> {
        return { std::move(g1), std::move(g2) };
    }
    template<typename T>
    auto operator << ( Generator<T>&& g1, T const& val ) -> Generator<T> {
        return { std::move(g1), value( val ) };
    }
    template<typename T>
    auto operator << ( T const& val, Generator<T>&& g2 ) -> Generator<T> {
        return { value( val ), std::move(g2) };
    }
    template<typename T, typename U>
    auto operator << ( Generator<T>&&, Generator<U>&& ) -> Generator<T> {
        // If you get an error here it's probably because you are trying to compose two generators that
        // are templated on different types.
        // Your error message will probably also show you the line calling this, which should give more context.
        return ComposedGeneratorsMustHaveTheSameUnderlyingType<T, U>();
    }

    template<typename T>
    struct Generators : GeneratorBase {
        std::vector<Generator<T>> m_generators;

        using type = T;

        Generators() : GeneratorBase( 0 ) {}

        void populate( T&& value ) {
            m_size += 1;
            m_generators.emplace_back( values( { std::move( value ) } ) );
        }
        template<typename U>
        void populate( U&& value ) {
            populate( T( std::move( value ) ) );
        }
        void populate( Generator<T>&& generator ) {
            m_size += generator.size();
            m_generators.emplace_back( std::move( generator ) );
        }

        template<typename U, typename... Gs>
        void populate( U&& valueOrGenerator, Gs... moreGenerators ) {
            populate( std::forward<U>( valueOrGenerator ) );
            populate( std::forward<Gs>( moreGenerators )... );
        }

        auto operator[]( size_t index ) const -> T {
            assert( index < m_size );

            size_t sizes = 0;
            for( auto const& gen : m_generators ) {
                auto localIndex = index-sizes;
                sizes += gen.size();
                if( index < sizes )
                    return gen[localIndex];
            }
            assert(false); // should never happen
            throw std::logic_error("this should never happen");
        }
    };

    template<typename T, typename... Gs>
    auto makeGenerators( Generator<T>&& generator, Gs... moreGenerators ) -> Generators<T> {
        Generators<T> generators;
        generators.m_generators.reserve( 1+sizeof...(Gs) );
        generators.populate( std::move( generator ), std::forward<Gs>( moreGenerators )... );
        return generators;
    }
    template<typename T>
    auto makeGenerators( Generator<T>&& generator ) -> Generators<T> {
        Generators<T> generators;
        generators.populate( std::move( generator ) );
        return generators;
    }
    template<typename T, typename... Gs>
    auto makeGenerators( T&& value, Gs... moreGenerators ) -> Generators<T> {
        return makeGenerators( values({ std::forward<T>( value ) }), std::forward<Gs>( moreGenerators )... );
    }
    template<typename T, typename U, typename... Gs>
    auto makeGenerators( U&& value, Gs... moreGenerators ) -> Generators<T> {
        return makeGenerators( values({ T( std::forward<U>( value ) ) }), std::forward<Gs>( moreGenerators )... );
    }

    class GeneratorCache {
        std::map<SourceLineInfo, GeneratorBasePtr> m_generators;

    public:

        template<typename T>
        auto add( SourceLineInfo const& lineInfo, Generators<T>&& generators ) -> Generators<T> const& {
            auto generatorPtr = pf::make_unique<Generators<T>>( std::move( generators ) );
            auto const& storedGenerator = *generatorPtr;
            m_generators.insert( { lineInfo, std::move( generatorPtr ) } );
            return storedGenerator;
        }

        template<typename T, typename L>
        auto getGenerator( SourceLineInfo const& lineInfo, L const& generatorExpression ) -> Generators<T> const& {

            auto it = m_generators.find( lineInfo );
            return ( it != m_generators.end() )
                ? static_cast<Generators<T> const&>( *it->second )
                : add( lineInfo, generatorExpression() );
        }
    };

    auto getGeneratorCache() -> GeneratorCache&;
    auto getIndexForGeneratorId( SourceLineInfo const& lineInfo, size_t size ) -> size_t;

    template<typename L>
    auto memoize( GeneratorCache& cache, SourceLineInfo const& lineInfo, L const& generatorExpression ) -> decltype(generatorExpression()) const& {

        using UnderlyingType = typename decltype(generatorExpression())::type;
        return cache.getGenerator<UnderlyingType, L>( lineInfo, generatorExpression );
    }

    template<typename L>
    auto generate( SourceLineInfo const& lineInfo, L const& generatorExpression ) -> typename decltype(generatorExpression())::type {
        // !TBD: how to do this without so many lookups?
        // hold cache in ResultCapture object? (so we don\t have to look up cache, then look up index)

        auto const& generator = memoize( getGeneratorCache(), lineInfo, generatorExpression );
        auto index = getIndexForGeneratorId( lineInfo, generator.size() );
        return generator[index];
    }

} // namespace generators
} // namespace Catch

#include "internal/catch_reenable_warnings.h"

#endif // TWOBLUECUBES_CATCH_GENERATORS_HPP_INCLUDED
