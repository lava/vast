#ifndef VAST_SEGMENT_H
#define VAST_SEGMENT_H

#include <vector>
#include <string>
#include "vast/aliases.h"
#include "vast/chunk.h"
#include "vast/cow.h"
#include "vast/time.h"
#include "vast/optional.h"
#include "vast/uuid.h"
#include "vast/io/compression.h"
#include "vast/util/operators.h"

namespace vast {

class event;

/// Contains a vector of chunks with additional meta data.
class segment : util::equality_comparable<segment>
{
public:
  /// A proxy class for writing into a segment. Each writer maintains a local
  /// chunk that receives events to serialize. Upon flushing, the writer
  /// appends the chunk to the underlying segment.
  class writer
  {
  public:
    /// Constructs a writer to serialize events into a segment.
    ///
    /// @param s The segment to write to.
    ///
    /// @param max_events_per_chunk The maximum number of events per chunk.
    ///
    /// @pre `s != nullptr`
    explicit writer(segment* s, size_t max_events_per_chunk = 0);

    /// Destructs a writer and flushes the event chunk into the underlying
    /// segment.
    ///
    /// @warning If the segment has no more room, flushing may fail and events
    /// may get lost.
    ~writer();

    /// Serializes an event into the underlying segment.
    /// @param e The event to write.
    /// @returns `true` on success and `false` if the segment is full.
    bool write(event const& e);

    /// Attaches the writer to a new segment.
    /// @param s The segment to attach the writer to.
    /// @pre `s != nullptr`
    void attach_to(segment* s);

    /// Seals the current chunk and appends it to the list of chunks in the
    /// underlying segment.
    ///
    /// @returns `false` on failure, `true` on success or if there
    /// were no events to flush.
    bool flush();

    /// Tests whether the underlying segment is full, i.e., can no longer take
    /// chunks because they would exceed the maximum segment size provided at
    /// construction time of this writer.
    ///
    /// @returns `true` *iff* the writer has a maximum segment size and the
    /// current chunk exceeds it.
    bool full() const;

    /// Retrieves the number of bytes processed in total.
    /// @returns The number of bytes written into this writer.
    size_t bytes() const;

  private:
    bool store(event const& e);

    segment* segment_;
    std::unique_ptr<chunk> chunk_;
    std::unique_ptr<chunk::writer> chunk_writer_;
    size_t max_events_per_chunk_;
  };

  /// A proxy class for reading from a segment. Multiple readers can safely
  /// access the same underlying segment.
  class reader
  {
  public:
    /// Constructs a reader for a specific segment.
    /// @param s The segment to read from.
    explicit reader(segment const* s);

    /// Retrieves the current position of the reader.
    /// @returns The ID of the next event to ::read.
    event_id position() const;

    /// Reads the next event from the current position.
    /// @param id If non-zero, specifies the ID of the event to extract.
    /// @returns An engaged event upon success.
    optional<event> read(event_id id = 0);

    /// Seeks to an event with a given ID.
    ///
    /// @param id The event ID to seek to.
    ///
    /// @returns `true` if seeking to *id* succeeded, and `false` if *id* is
    /// out-of-bounds.
    ///
    /// @post The next call to ::read exctracts the event with ID *id*.
    bool seek(event_id id);

  private:
    /// Extracts events according to given boundaries.
    ///
    /// @param from The ID where to start extraction. If 0, will use the
    /// current position of the reader.
    ///
    /// @param to The ID where to end extraction. If 0, will extract until the
    /// end of the current chunk.
    ///
    /// @param f The function to invoke on each extracted event.
    ///
    /// @returns An engaged value with the number of times *f* has been
    /// applied, and a disengaged value if an error occurred.
    optional<size_t> extract(event_id from,
                             event_id to,
                             std::function<void(event)> f);

    /// Moves to the next chunk.
    /// @returns A pointer to the next chunk or `nullptr` on failure.
    chunk const* next();

    /// Moves to the previous chunk.
    /// @returns A pointer to the previous chunk or `nullptr` on failure.
    chunk const* prev();

    /// Resets the internal reading position to the beginning of the current
    /// chunk.
    ///
    /// @returns The number of events backed up.
    event_id backup();

    /// Skips over a given number of events.
    /// @param n The number of events to skip.
    /// @returns The number of events actually skipped.
    event_id skip(size_t n);

    /// Loads the next event.
    /// @param e The event to load into. If `nullptr`, equivalent to skipping.
    /// @returns `true` on success.
    bool load(event* e);

    /// Checks whether a given ID falls into the current chunk.
    /// @param eid The event ID to check.
    /// @returns `true` if *eid* falls into the current chunk.
    bool within_current_chunk(event_id eid) const;

    segment const& segment_;
    chunk const* current_ = nullptr;
    event_id next_ = 0;
    event_id chunk_base_ = 0;
    size_t chunk_idx_ = 0;
    std::unique_ptr<chunk::reader> chunk_reader_;
  };

  static uint32_t const magic = 0x2a2a2a2a;
  static uint8_t const version = 1;

  /// Constructs a segment.
  /// @param id The UUID of the segment.
  /// @param max_bytes The maximum segment size.
  /// @param method The compression method to use for each chunk.
  segment(uuid id = uuid::nil(), uint64_t max_bytes = 0,
          io::compression method = io::lz4);

  /// Retrieves the segment ID.
  /// @returns A UUID identifying the segment.
  uuid const& id() const;

  /// Sets the segment base ID for events.
  /// @param id The base event ID for this segment.
  void base(event_id id);

  /// Retrieves the segment base ID for events.
  /// @returns The base event ID for this segment.
  event_id base() const;

  /// Checks whether the segment contains the event with the given ID.
  /// @param eid The event ID to check.
  /// @returns `true` iff the segment contains the event having id *eid*.
  bool contains(event_id eid) const;

  /// Checks whether the segment contains the given event half-open ID range.
  /// @param from The left side of the interval.
  /// @param to The right side of the interval.
  /// @returns `true` iff the segment contains *[from, to]*.
  bool contains(event_id from, event_id to) const;

  /// Retrieves the number of events in the segment.
  uint32_t events() const;

  /// Retrieves the number of bytes the segment occupies in memory.
  uint64_t bytes() const;

  /// Retrieves the maximum number of bytes this segment can occupy.
  ///
  /// @returns The maximum number of bytes this segment can occupy or 0 if its
  /// size is unbounded.
  uint64_t max_bytes() const;

  /// Extracts a single event with a given ID.
  /// @param id The ID of the event.
  /// @returns The event having ID *id* or an disengaged option otherwise.
  optional<event> load(event_id id) const;

  /// Writes a vector of events into the segment.
  /// @param v The vector of events to write.
  /// @param max_events_per_chunk The maximum number of events per chunk.
  /// @returns The number of events successfully written.
  size_t store(std::vector<event> const& v, size_t max_events_per_chunk = 0);

  /// Writes a sequence of events into the segment.
  /// @param begin The start iterator.
  /// @param end The one-past-the-end iterator.
  /// @param max_events_per_chunk The maximum number of events per chunk.
  /// @returns The number of events successfully written.
  template <typename Iterator>
  size_t store(Iterator begin, Iterator end, size_t max_events_per_chunk = 0)
  {
    writer w(this, max_events_per_chunk);

    size_t n;
    while (begin != end)
    {
      if (! w.write(*begin++))
        break;
      ++n;
    }

    return n;
  }

private:
  uuid id_;
  io::compression compression_;
  event_id base_ = 0;
  uint32_t n_ = 0;
  uint64_t max_bytes_ = 0;
  uint64_t occupied_bytes_ = 0;
  std::vector<cow<chunk>> chunks_;

private:
  friend access;
  void serialize(serializer& sink) const;
  void deserialize(deserializer& source);

  friend bool operator==(segment const& x, segment const& y);
};

} // namespace vast

#endif
