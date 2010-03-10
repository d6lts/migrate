<?php
// $Id$

/**
 * @file
 * Support for taxonomy term destinations.
 */

// TODO:
// Make sure this works with updates, explicit destination keys
// taxonomy_term_save() is doing a cache_clear_all and an automatic insertion for parent.

/**
 * Destination class implementing migration into terms.
 */
class MigrateDestinationTerm extends MigrateDestinationEntity {
  /**
   * Basic initialization
   */
  public function __construct($bundle, $language = LANGUAGE_NONE, $input_format = NULL) {
    // PK definition, for use in constructing map/message tables
    $destination_key = array(
      'tid' => array(
        'type' => 'int',
        'unsigned' => TRUE,
        'not null' => TRUE,
        'description' => 'ID of destination term',
      ),
    );
    parent::__construct($destination_key, 'taxonomy_term', $bundle, $language, $input_format);
  }

  /**
   * Returns a list of fields available to be mapped for this vocabulary (bundle)
   *
   * @return array
   *  Keys: machine names of the fields (to be passed to addFieldMapping)
   *  Values: Human-friendly descriptions of the fields.
   */
  public function fields() {
    $fields = array();
    // First the core (term_data table) properties
    $fields['tid'] = t('Term: Existing term ID');
    $fields['name'] = t('Term: Name');
    $fields['description'] = t('Term: Description');
    $fields['format'] = t('Term: Format');
    $fields['weight'] = t('Term: Weight');
    // TODO: Only if vocab has hierarchy
    $fields['parent_name'] = t('Term: Parent (by name)');
    $fields['parent'] = t('Term: Parent (by Drupal ID)');

    // Then add any fields attached to this vocabulary (bundle)
    $field_instance_info = field_info_instances($this->entityType, $this->bundle);
    foreach ($field_instance_info as $machine_name => $instance) {
      $field_info = field_info_field($machine_name);

      $fields[$machine_name] = t('Term:') . ' ' . $instance['label'] .
        ' (' . $field_info['type'] . ')';
    }

    // Then add in anything provided by handlers
    $fields += migrate_handler_invoke_all('Term', 'fields');

    return $fields;
  }

  /**
   * Delete a migrated term
   *
   * @param $migration
   *  Controlling migration object. Unused here.
   * @param $ids
   *  Array of fields representing the key (in this case, just tid).
   */
  public function rollback(Migration $migration, array $key) {
    migrate_instrument_start('taxonomy_term_delete');
    $tid = $key[0];

    /*
     * This load() happens soon delete() anyway. We load here in order to
     * avoid notices when term has already been deleted. That is easily possible
     * considering how deleting a term parent also deletes children in same call.
     */
    if (taxonomy_term_load($tid)) {
      $result = (bool) taxonomy_term_delete($tid);
      return $result;
    }
    migrate_instrument_stop('taxonomy_term_delete');
  }

  /**
   * Import a single term.
   *
   * @param $migration
   *  Controlling migration object. Passed through to prepare/complete handlers.
   * @param $values
   *  Term object to build. Prefilled with any fields mapped in the Migration.
   * @param $row
   *  Raw source data object - passed through to prepare/complete handlers.
   * @return array
   *  Array of key fields (tid only in this case) of the term that was saved if
   *  successful. FALSE on failure.
   */
  public function import(Migration $migration, stdClass $values, stdClass $row, array $arguments) {
    // Handle update case
    if (isset($row->migrate_map_destid1)) {
      $values->tid = $row->migrate_map_destid1;
    }

    // Default to bundle if no vocabulary machine name provided
    if (!isset($values->vocabulary_machine_name)) {
      $values->vocabulary_machine_name = $this->bundle;
    }
    // vid is required
    if (empty($values->vid)) {
      static $vocab_map = array();
      if (!isset($vocab_map[$values->vocabulary_machine_name])) {
        // The keys of the returned array are vids
        $vocabs = taxonomy_vocabulary_load_multiple(array(),
          array('machine_name' => $values->vocabulary_machine_name));
        $vids = array_keys($vocabs);
        if (isset($vids[0])) {
          $vocab_map[$values->vocabulary_machine_name] = $vids[0];
        }
        else {
          $migration->saveMessage(t('No vocabulary found with machine_name !name',
            array('!name' => $values->vocabulary_machine_name)));
          return FALSE;
        }
      }
      $values->vid = $vocab_map[$values->vocabulary_machine_name];
    }
    // Look up parent name if provided
    if (isset($values->parent_name)) {
      // Look for the name in the same vocabulary.
      // Note that hierarchies may have multiples of the same name...
      $terms = taxonomy_term_load_multiple(array(),
        array('name' => trim($values->parent_name), 'vid' => $values->vid));
      $tids = array_keys($terms);
      $values->parent = $tids[0];
      unset($values->parent_name);
    }
    if (!isset($values->parent)) {
      $values->parent = array(0);
    }
    if (!isset($values->format)) {
      $values->format = $this->inputFormat;
    }

    // Look for Field API fields and handle appropriately
    $this->prepareFields($values, $arguments);

    migrate_handler_invoke_all('Term', 'prepare', $migration, $values, $row);

    migrate_instrument_start('taxonomy_term_save');
    $status = taxonomy_term_save($values);
    migrate_instrument_stop('taxonomy_term_save');
    migrate_handler_invoke_all('Term', 'complete', $migration, $values, $row);
    $return = isset($values->tid) ? array($values->tid) : FALSE;
    return $return;
  }
}