<?php
// $Id$

/**
 * @file
 * Define a MigrateSource for importing from Microsoft SQL Server databases. This
 * plugin has limited capabilities compared to MigrateSourceSQL which can join
 * with the map/message tables and only fetch un-migrated content. This plugin
 * fetched everything unless an idlist or itemlimit are supplied. Note that
 * this is not completely inefficient; already migrated content does not actually
 * get re-imported, it just gets re-fetched.
 */

/**
 * Implementation of MigrateSource, to handle imports from remote DB servers.
 */
class MigrateSourceMSSQL extends MigrateSource {
  /**
   * Array containing information for connecting to SQL Server:
   *  servername - Hostname of the SQL Server
   *  username - Username to connect as
   *  password - Password for logging in
   *  database (optional) - Database to select after connecting
   *
   * @var array
   */
  protected $configuration;

  /**
   * The active MS SQL Server connection for this source.
   *
   * @var resource
   */
  protected $connection;
  /**
   * The SQL query from which to obtain data. Is a string.
   */
  protected $query;

  /**
   * The result object from executing the query - traversed to process the
   * incoming data.
   */
  protected $result;

  /**
   * By default, mssql_query fetches all results - severe memory problems with
   * big tables. So, we will fetch a batch at a time.
   *
   * @var int
   */
  public $batchSize;

  /**
   * Simple initialization.
   */
  public function __construct(array $configuration, $query, $count_query,
      array $key_schema, array $fields, $batch_size = 500) {
    parent::__construct($key_schema);
    $this->query = $query;
    $this->countQuery = $count_query;
    $this->configuration = $configuration;
    $this->fields = $fields;
    $this->batchSize = $batch_size;
  }

  /**
   * Return a string representing the source query.
   *
   * @return string
   */
  public function __toString() {
    return (string) $this->query;
  }

  /**
   * Connect lazily to the DB server.
   */
  protected function connect() {
    if (!isset($this->connection)) {
      $this->connection = mssql_connect(
        $this->configuration['servername'],
        $this->configuration['username'],
        $this->configuration['password'],
        TRUE);
      if (isset($this->configuration['database'])) {
        mssql_select_db($this->configuration['database'], $this->connection);
      }
    }
  }

  /**
   * Returns a list of fields available to be mapped from the source query.
   *
   * @return array
   *  Keys: machine names of the fields (to be passed to addFieldMapping)
   *  Values: Human-friendly descriptions of the fields.
   */
  public function fields() {
    // The fields are defined in the Contructor for this plugin.
    return $this->fields;
  }

  /**
   * Return a count of all available source records.
   *
   * @param boolean $refresh
   *  If TRUE, or if there is no cached count, perform a SQL COUNT query to
   *  retrieve and cache the number of rows in the query. Otherwise, return
   *  the last cached value.
   */
  public function count($refresh = FALSE) {
    if ($refresh) {
      cache_clear_all(__METHOD__, 'cache');
    }

    $count = cache_get(__METHOD__, 'cache');
    if ($count === FALSE) {
      // Cache miss.
      $this->connect();
      $result = mssql_query($this->countQuery);
      $count = reset(mssql_fetch_object($result));
      $return = cache_set(__METHOD__, $count, 'cache');
    }
    else {
      $count = $count->data;
    }
    return $count;
  }

  /**
   * Fetch the next row from the source query.
   *
   * @param Migration $migration
   *  The migration object controlling this process.
   * @param array $options
   *  Options for filtering the query:
   *   itemlimit - If present, process no more than this many items.
   *   idlist - If present, process only items with source keys in this
   *     comma-separated list. NOTE: Only supported for sources with single-column
   *     keys.
   */
  public function fetch(Migration $migration, array $options = array()) {
    // Do all the hard work on the first call
    if (!isset($this->result)) {
      // First, if we support syncing of updated source rows, check for rows
      // that need syncing
      if ($migration->highwaterField) {
        // Get highwater from Migration object (migrate_status table)
        $highwater = $migration->highwater();

        // Plug in to query
        $this->query = str_replace(':highwater', $highwater, $this->query);
      }

      $keys = array();

      foreach ($this->keySchema as $field_name => $field_schema) {
        // Allow caller to provide an alias to table containing the primary key.
        // TODO: Alias should be determined automatically
        if (!empty($field_schema['alias'])) {
          $field_name = $field_schema['alias'] . '.' . $field_name;
        }
        $keys[] = $field_name;
      }
      if (isset($options['idlist'])) {
        // TODO: Sanitize. not critical as this is admin supplied data in drush.
        $where[] = $keys[0] . ' IN (' . $options['idlist'] . ')';
      }

      migrate_instrument_start('mssql_query');
      $this->connect();
      if (!$this->result = mssql_query($this->query, $this->connection, $this->batchSize)) {
        migrate_instrument_stop('mssql_query');
        throw new Exception('MSSQL fetch query failed: ' . $this->query);
      }
      migrate_instrument_stop('mssql_query');
    }

    migrate_instrument_start('mssql_fetch_object');
    $data_row = mssql_fetch_object($this->result);
    migrate_instrument_stop('mssql_fetch_object');
    // Might be totally out of data, or just out of this batch - request another
    // batch and see
    if (!$data_row) {
      migrate_instrument_start('mssql_fetch_batch');
      mssql_fetch_batch($this->result);
      migrate_instrument_stop('mssql_fetch_batch');

      migrate_instrument_start('mssql_fetch_object');
      $data_row = mssql_fetch_object($this->result);
      migrate_instrument_stop('mssql_fetch_object');
    }
    return $data_row;
  }
}