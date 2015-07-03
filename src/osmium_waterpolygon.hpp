#include <osmium/area/multipolygon_collector.hpp>
#include <osmium/area/assembler.hpp>

using namespace std;

/***
 * The WaterpolygonCollector is just collecting the areas and call them back to the AreaHandler.
 */
template <class TAssembler>
class WaterpolygonCollector : public osmium::relations::Collector<WaterpolygonCollector<TAssembler>, false, true, false> {

    typedef typename osmium::relations::Collector<WaterpolygonCollector<TAssembler>, false, true, false> collector_type;

    typedef typename TAssembler::config_type assembler_config_type;
    const assembler_config_type m_assembler_config;

    osmium::memory::Buffer m_output_buffer;

    static constexpr size_t initial_output_buffer_size = 1024 * 1024;
    static constexpr size_t max_buffer_size_for_flush = 100 * 1024;

    DataStorage *ds;

    void flush_output_buffer() {
        if (this->callback()) {
            osmium::memory::Buffer buffer(initial_output_buffer_size);
            std::swap(buffer, m_output_buffer);
            this->callback()(std::move(buffer));
        }
    }

    void possibly_flush_output_buffer() {
        if (m_output_buffer.committed() > max_buffer_size_for_flush) {
            flush_output_buffer();
        }
    }

public:

    explicit WaterpolygonCollector(const assembler_config_type& assembler_config, DataStorage *datastorage) :
        collector_type(),
        m_assembler_config(assembler_config),
        m_output_buffer(initial_output_buffer_size, osmium::memory::Buffer::auto_grow::yes),
        ds(datastorage){
    }

    bool keep_relation(const osmium::Relation& relation) {
        const char* type = relation.tags().get_value_by_key("type");
        const char* natural = relation.tags().get_value_by_key("natural");
        const char* landuse = relation.tags().get_value_by_key("landuse");

        if (!type) {
            return false;
        }
        if ((strcmp(type, "multipolygon")) && (strcmp(type, "boundary"))) {
            return false;
        }
        if ((natural) && (!strcmp(natural, "water"))) {
            return true;
        }
        if (relation.tags().get_value_by_key("waterway")) {
            return true;
        }
        if ((landuse) && (!strcmp(landuse, "reservoir"))) {
            return true;
        }
        if ((landuse) && (!strcmp(landuse, "basin"))) {
            return true;
        }
        return false;
    }

    bool way_is_valid(const osmium::Way& way) {
        const char* type = way.tags().get_value_by_key("type");
        const char* natural = way.tags().get_value_by_key("natural");
        const char* landuse = way.tags().get_value_by_key("landuse");

        /*if (!type) {
            return false;
        }
        if ((strcmp(type, "multipolygon")) && (strcmp(type, "boundary"))) {
            return false;
        }*/
        if ((natural) && (!strcmp(natural, "water"))) {
            return true;
        }
        if (way.tags().get_value_by_key("waterway")) {
            return true;
        }
        if ((landuse) && (!strcmp(landuse, "reservoir"))) {
            return true;
        }
        if ((landuse) && (!strcmp(landuse, "basin"))) {
            return true;
        }
        return false;
    }

    bool keep_member(const osmium::relations::RelationMeta& /*relation_meta*/, const osmium::RelationMember& member) {
        // We are only interested in members of type way.
        return member.type() == osmium::item_type::way;
    }

    void complete_relation(osmium::relations::RelationMeta& relation_meta) {
        const osmium::Relation& relation = this->get_relation(relation_meta);
        std::vector<size_t> offsets;
        for (const auto& member : relation.members()) {
            if (member.ref() != 0) {
                offsets.push_back(this->get_offset(member.type(), member.ref()));
            }
        }
        try {
            TAssembler assembler(m_assembler_config);
            assembler(relation, offsets, this->members_buffer(), m_output_buffer);
            possibly_flush_output_buffer();
        } catch (osmium::invalid_location&) {
            // XXX ignore
        }

        for (const auto& member : relation.members()) {
            if (member.ref() != 0) {
                auto& mmv = this->member_meta(member.type());
                auto range = std::equal_range(mmv.begin(), mmv.end(), osmium::relations::MemberMeta(member.ref()));
                assert(range.first != range.second);

                // if this is the last time this object was needed
                // then mark it as removed
                if (osmium::relations::count_not_removed(range.first, range.second) == 1) {
                    this->get_member(range.first->buffer_offset()).set_removed(true);
                }

                for (auto it = range.first; it != range.second; ++it) {
                    if (!it->removed() && relation.id() == this->get_relation(it->relation_pos()).id()) {
                        it->remove();
                        break;
                    }
                }
            }
        }
    }

    void way_not_in_any_relation(const osmium::Way& way) {
        if (way_is_valid(way)) {
            if (way.nodes().size() > 3 && way.ends_have_same_location()) {
                // way is closed and has enough nodes, build simple multipolygon
                try {
                    TAssembler assembler(m_assembler_config);
                    assembler(way, m_output_buffer);
                    possibly_flush_output_buffer();
                } catch (osmium::invalid_location&) {
                    // XXX ignore
                }
            }
        }
    }

    void flush() {
        flush_output_buffer();
    }

    osmium::memory::Buffer read() {
        osmium::memory::Buffer buffer(initial_output_buffer_size, osmium::memory::Buffer::auto_grow::yes);
        std::swap(buffer, m_output_buffer);
        return buffer;
    }

};


