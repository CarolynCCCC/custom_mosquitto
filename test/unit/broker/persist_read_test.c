/* Tests for persistence.
 *
 * FIXME - these need to be aggressive about finding failures, at the moment
 * they are just confirming that good behaviour works. */

#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include "path_helper.h"

#include "mosquitto_broker_internal.h"
#include "persist.h"
#include "property_mosq.h"
#include "property_common.h"

char *last_sub = NULL;
int last_qos;
uint32_t last_identifier;

struct mosquitto_db db;

static void test_cleanup(void)
{
	struct mosquitto *ctxt, *ctxt_tmp;

	HASH_ITER(hh_id, db.contexts_by_id, ctxt, ctxt_tmp){
		HASH_DELETE(hh_id, db.contexts_by_id, ctxt);
		mosquitto_free(ctxt->username);
		mosquitto_free(ctxt->id);
		db__messages_delete(ctxt, true);
		mosquitto_free(ctxt);
	}
	db__close();
}

static void TEST_persistence_disabled(void)
{
	struct mosquitto__config config;
	int rc;

	memset(&db, 0, sizeof(struct mosquitto_db));
	memset(&config, 0, sizeof(struct mosquitto__config));
	db.config = &config;

	rc = persist__restore();
	CU_ASSERT_EQUAL(rc, MOSQ_ERR_SUCCESS);
	test_cleanup();
}


static void TEST_empty_file(void)
{
	struct mosquitto__config config;
	int rc;

	memset(&db, 0, sizeof(struct mosquitto_db));
	memset(&config, 0, sizeof(struct mosquitto__config));
	db.config = &config;

	config.persistence = true;

	char persistence_filepath[4096];
	cat_sourcedir_with_relpath(persistence_filepath, "/files/persist_read/empty.test-db");
	config.persistence_filepath = persistence_filepath;

	rc = persist__restore();
	test_cleanup();
	CU_ASSERT_EQUAL(rc, MOSQ_ERR_SUCCESS);
}


static void TEST_corrupt_header(void)
{
	struct mosquitto__config config;
	int rc;

	memset(&db, 0, sizeof(struct mosquitto_db));
	memset(&config, 0, sizeof(struct mosquitto__config));
	db.config = &config;

	config.persistence = true;

	char persistence_filepath[4096];
	cat_sourcedir_with_relpath(persistence_filepath, "/files/persist_read/corrupt-header-short.test-db");
	config.persistence_filepath = persistence_filepath;
	rc = persist__restore();
	CU_ASSERT_EQUAL(rc, 1);

	cat_sourcedir_with_relpath(persistence_filepath, "/files/persist_read/corrupt-header-long.test-db");
	config.persistence_filepath = persistence_filepath;
	rc = persist__restore();
	CU_ASSERT_EQUAL(rc, 1);
}

static void TEST_unsupported_version(void)
{
	struct mosquitto__config config;
	int rc;

	memset(&db, 0, sizeof(struct mosquitto_db));
	memset(&config, 0, sizeof(struct mosquitto__config));
	db.config = &config;

	config.persistence = true;
	char persistence_filepath[4096];
	cat_sourcedir_with_relpath(persistence_filepath, "/files/persist_read/unsupported-version.test-db");
	config.persistence_filepath = persistence_filepath;

	rc = persist__restore();
	CU_ASSERT_EQUAL(rc, 1);
}


static void TEST_v3_config_ok(void)
{
	struct mosquitto__config config;
	int rc;

	memset(&db, 0, sizeof(struct mosquitto_db));
	memset(&config, 0, sizeof(struct mosquitto__config));
	db.config = &config;

	config.persistence = true;
	char persistence_filepath[4096];
	cat_sourcedir_with_relpath(persistence_filepath, "/files/persist_read/v3-cfg.test-db");
	config.persistence_filepath = persistence_filepath;

	rc = persist__restore();
	CU_ASSERT_EQUAL(rc, MOSQ_ERR_SUCCESS);
	CU_ASSERT_EQUAL(db.last_db_id, 0x7856341200000000);
}


static void TEST_v4_config_ok(void)
{
	struct mosquitto__config config;
	int rc;

	memset(&db, 0, sizeof(struct mosquitto_db));
	memset(&config, 0, sizeof(struct mosquitto__config));
	db.config = &config;

	config.persistence = true;
	char persistence_filepath[4096];
	cat_sourcedir_with_relpath(persistence_filepath, "/files/persist_read/v4-cfg.test-db");
	config.persistence_filepath = persistence_filepath;

	rc = persist__restore();
	CU_ASSERT_EQUAL(rc, MOSQ_ERR_SUCCESS);
	CU_ASSERT_EQUAL(db.last_db_id, 0x7856341200000000);
}


static void TEST_v3_config_truncated(void)
{
	struct mosquitto__config config;
	int rc;

	memset(&db, 0, sizeof(struct mosquitto_db));
	memset(&config, 0, sizeof(struct mosquitto__config));
	db.config = &config;

	config.persistence = true;
	char persistence_filepath[4096];
	cat_sourcedir_with_relpath(persistence_filepath, "/files/persist_read/v3-cfg-truncated.test-db");
	config.persistence_filepath = persistence_filepath;

	rc = persist__restore();
	CU_ASSERT_EQUAL(rc, 1);
	CU_ASSERT_EQUAL(db.last_db_id, 0);
}


static void TEST_v3_config_bad_dbid(void)
{
	struct mosquitto__config config;
	int rc;

	memset(&db, 0, sizeof(struct mosquitto_db));
	memset(&config, 0, sizeof(struct mosquitto__config));
	db.config = &config;

	config.persistence = true;
	char persistence_filepath[4096];
	cat_sourcedir_with_relpath(persistence_filepath, "/files/persist_read/v3-cfg-bad-dbid.test-db");
	config.persistence_filepath = persistence_filepath;

	rc = persist__restore();
	CU_ASSERT_EQUAL(rc, 1);
	CU_ASSERT_EQUAL(db.last_db_id, 0);
}


static void TEST_v3_bad_chunk(void)
{
	struct mosquitto__config config;
	int rc;

	memset(&db, 0, sizeof(struct mosquitto_db));
	memset(&config, 0, sizeof(struct mosquitto__config));
	db.config = &config;

	config.persistence = true;
	char persistence_filepath[4096];
	cat_sourcedir_with_relpath(persistence_filepath, "/files/persist_read/v3-bad-chunk.test-db");
	config.persistence_filepath = persistence_filepath;

	rc = persist__restore();
	CU_ASSERT_EQUAL(rc, MOSQ_ERR_SUCCESS);
	CU_ASSERT_EQUAL(db.last_db_id, 0x17);
}


static void TEST_v3_message_store(void)
{
	struct mosquitto__config config;
	int rc;

	memset(&db, 0, sizeof(struct mosquitto_db));
	memset(&config, 0, sizeof(struct mosquitto__config));
	db.config = &config;

	config.persistence = true;
	char persistence_filepath[4096];
	cat_sourcedir_with_relpath(persistence_filepath, "/files/persist_read/v3-message-store.test-db");
	config.persistence_filepath = persistence_filepath;

	rc = persist__restore();
	CU_ASSERT_EQUAL(rc, MOSQ_ERR_SUCCESS);
	CU_ASSERT_EQUAL(db.msg_store_count, 1);
	CU_ASSERT_EQUAL(db.msg_store_bytes, 7);
	CU_ASSERT_PTR_NOT_NULL(db.msg_store);
	if(db.msg_store){
		CU_ASSERT_EQUAL(db.msg_store->data.store_id, 1);
		CU_ASSERT_STRING_EQUAL(db.msg_store->data.source_id, "source_id");
		CU_ASSERT_EQUAL(db.msg_store->data.source_mid, 2);
		CU_ASSERT_EQUAL(db.msg_store->data.qos, 2);
		CU_ASSERT_EQUAL(db.msg_store->data.retain, 1);
		CU_ASSERT_PTR_NOT_NULL(db.msg_store->data.topic);
		if(db.msg_store->data.topic){
			CU_ASSERT_STRING_EQUAL(db.msg_store->data.topic, "topic");
		}
		CU_ASSERT_EQUAL(db.msg_store->data.payloadlen, 7);
		if(db.msg_store->data.payloadlen == 7){
			CU_ASSERT_NSTRING_EQUAL(db.msg_store->data.payload, "payload", 7);
		}
	}
	test_cleanup();
}

static void TEST_v3_client(void)
{
	struct mosquitto__config config;
	struct mosquitto *context;
	int rc;

	memset(&db, 0, sizeof(struct mosquitto_db));
	memset(&config, 0, sizeof(struct mosquitto__config));
	db.config = &config;

	config.persistence = true;
	char persistence_filepath[4096];
	cat_sourcedir_with_relpath(persistence_filepath, "/files/persist_read/v3-client.test-db");
	config.persistence_filepath = persistence_filepath;

	rc = persist__restore();
	CU_ASSERT_EQUAL(rc, MOSQ_ERR_SUCCESS);

	CU_ASSERT_PTR_NOT_NULL(db.contexts_by_id);
	HASH_FIND(hh_id, db.contexts_by_id, "client-id", strlen("client-id"), context);
	CU_ASSERT_PTR_NOT_NULL(context);
	if(context){
		CU_ASSERT_PTR_NULL(context->msgs_in.inflight);
		CU_ASSERT_PTR_NULL(context->msgs_out.inflight);
		CU_ASSERT_EQUAL(context->last_mid, 0x5287);
	}
	test_cleanup();
}

static void TEST_v3_client_message(void)
{
	struct mosquitto__config config;
	struct mosquitto *context;
	int rc;

	memset(&db, 0, sizeof(struct mosquitto_db));
	memset(&config, 0, sizeof(struct mosquitto__config));
	db.config = &config;

	config.persistence = true;
	char persistence_filepath[4096];
	cat_sourcedir_with_relpath(persistence_filepath, "/files/persist_read/v3-client-message.test-db");
	config.persistence_filepath = persistence_filepath;
	config.max_inflight_messages = 20;

	rc = persist__restore();
	CU_ASSERT_EQUAL(rc, MOSQ_ERR_SUCCESS);

	CU_ASSERT_PTR_NOT_NULL(db.contexts_by_id);
	HASH_FIND(hh_id, db.contexts_by_id, "client-id", strlen("client-id"), context);
	CU_ASSERT_PTR_NOT_NULL(context);
	if(context){
		CU_ASSERT_PTR_NOT_NULL(context->msgs_out.inflight);
		if(context->msgs_out.inflight){
			CU_ASSERT_PTR_NULL(context->msgs_out.inflight->next);
			CU_ASSERT_PTR_NOT_NULL(context->msgs_out.inflight->base_msg);
			if(context->msgs_out.inflight->base_msg){
				CU_ASSERT_EQUAL(context->msgs_out.inflight->base_msg->ref_count, 1);
				CU_ASSERT_STRING_EQUAL(context->msgs_out.inflight->base_msg->data.source_id, "source_id");
				CU_ASSERT_EQUAL(context->msgs_out.inflight->base_msg->data.source_mid, 2);
				CU_ASSERT_EQUAL(context->msgs_out.inflight->base_msg->data.qos, 2);
				CU_ASSERT_EQUAL(context->msgs_out.inflight->base_msg->data.retain, 1);
				CU_ASSERT_PTR_NOT_NULL(context->msgs_out.inflight->base_msg->data.topic);
				if(context->msgs_out.inflight->base_msg->data.topic){
					CU_ASSERT_STRING_EQUAL(context->msgs_out.inflight->base_msg->data.topic, "topic");
				}
				CU_ASSERT_EQUAL(context->msgs_out.inflight->base_msg->data.payloadlen, 7);
				if(context->msgs_out.inflight->base_msg->data.payloadlen == 7){
					CU_ASSERT_NSTRING_EQUAL(context->msgs_out.inflight->base_msg->data.payload, "payload", 7);
				}
			}
			CU_ASSERT_EQUAL(context->msgs_out.inflight->data.mid, 0x73);
			CU_ASSERT_EQUAL(context->msgs_out.inflight->data.qos, 1);
			CU_ASSERT_EQUAL(context->msgs_out.inflight->data.retain, 0);
			CU_ASSERT_EQUAL(context->msgs_out.inflight->data.direction, mosq_md_out);
			CU_ASSERT_EQUAL(context->msgs_out.inflight->data.state, mosq_ms_wait_for_puback);
			CU_ASSERT_EQUAL(context->msgs_out.inflight->data.dup, 0);
			CU_ASSERT_EQUAL(context->msgs_out.inflight->data.subscription_identifier, 0);
		}
	}
	test_cleanup();
}

static void TEST_v3_retain(void)
{
	struct mosquitto__config config;
	int rc;

	memset(&db, 0, sizeof(struct mosquitto_db));
	memset(&config, 0, sizeof(struct mosquitto__config));
	db.config = &config;

	retain__init();
	config.persistence = true;
	char persistence_filepath[4096];
	cat_sourcedir_with_relpath(persistence_filepath, "/files/persist_read/v3-retain.test-db");
	config.persistence_filepath = persistence_filepath;

	rc = persist__restore();
	CU_ASSERT_EQUAL(rc, MOSQ_ERR_SUCCESS);
	CU_ASSERT_EQUAL(db.msg_store_count, 1);
	CU_ASSERT_EQUAL(db.msg_store_bytes, 7);
	CU_ASSERT_PTR_NOT_NULL(db.msg_store);
	if(db.msg_store){
		CU_ASSERT_EQUAL(db.msg_store->data.store_id, 0x54);
		CU_ASSERT_STRING_EQUAL(db.msg_store->data.source_id, "source_id");
		CU_ASSERT_EQUAL(db.msg_store->data.source_mid, 2);
		CU_ASSERT_EQUAL(db.msg_store->data.qos, 2);
		CU_ASSERT_EQUAL(db.msg_store->data.retain, 1);
		CU_ASSERT_PTR_NOT_NULL(db.msg_store->data.topic);
		if(db.msg_store->data.topic){
			CU_ASSERT_STRING_EQUAL(db.msg_store->data.topic, "topic");
		}
		CU_ASSERT_EQUAL(db.msg_store->data.payloadlen, 7);
		if(db.msg_store->data.payloadlen == 7){
			CU_ASSERT_NSTRING_EQUAL(db.msg_store->data.payload, "payload", 7);
		}
	}
	CU_ASSERT_PTR_NOT_NULL(db.retains);
	if(db.retains){
		CU_ASSERT_STRING_EQUAL(db.retains->topic, "");
		CU_ASSERT_PTR_NOT_NULL(db.retains->children);
		if(db.retains->children){
			CU_ASSERT_STRING_EQUAL(db.retains->children->topic, "");
			CU_ASSERT_PTR_NOT_NULL(db.retains->children->children);
			if(db.retains->children->children){
				CU_ASSERT_STRING_EQUAL(db.retains->children->children->topic, "topic");
			}
		}
	}
	test_cleanup();
}

static void TEST_v3_sub(void)
{
	struct mosquitto__config config;
	struct mosquitto *context;
	int rc;

	last_sub = NULL;
	last_qos = -1;

	memset(&db, 0, sizeof(struct mosquitto_db));
	memset(&config, 0, sizeof(struct mosquitto__config));
	db.config = &config;

	config.persistence = true;
	char persistence_filepath[4096];
	cat_sourcedir_with_relpath(persistence_filepath, "/files/persist_read/v3-sub.test-db");
	config.persistence_filepath = persistence_filepath;

	rc = persist__restore();
	CU_ASSERT_EQUAL(rc, MOSQ_ERR_SUCCESS);

	CU_ASSERT_PTR_NOT_NULL(db.contexts_by_id);
	HASH_FIND(hh_id, db.contexts_by_id, "client-id", strlen("client-id"), context);
	CU_ASSERT_PTR_NOT_NULL(context);
	if(context){
		CU_ASSERT_PTR_NOT_NULL(last_sub);
		if(last_sub){
			CU_ASSERT_STRING_EQUAL(last_sub, "subscription")
			free(last_sub);
		}
		CU_ASSERT_EQUAL(last_qos, 1);
	}
	test_cleanup();
}

static void TEST_v4_message_store(void)
{
	struct mosquitto__config config;
	int rc;

	memset(&db, 0, sizeof(struct mosquitto_db));
	memset(&config, 0, sizeof(struct mosquitto__config));
	db.config = &config;

	config.persistence = true;
	char persistence_filepath[4096];
	cat_sourcedir_with_relpath(persistence_filepath, "/files/persist_read/v4-message-store.test-db");
	config.persistence_filepath = persistence_filepath;

	rc = persist__restore();
	CU_ASSERT_EQUAL(rc, MOSQ_ERR_SUCCESS);
	CU_ASSERT_EQUAL(db.msg_store_count, 1);
	CU_ASSERT_EQUAL(db.msg_store_bytes, 7);
	CU_ASSERT_PTR_NOT_NULL(db.msg_store);
	if(db.msg_store){
		CU_ASSERT_EQUAL(db.msg_store->data.store_id, 0xFEDCBA9876543210);
		CU_ASSERT_STRING_EQUAL(db.msg_store->data.source_id, "source_id");
		CU_ASSERT_EQUAL(db.msg_store->data.source_mid, 0x88);
		CU_ASSERT_EQUAL(db.msg_store->data.qos, 1);
		CU_ASSERT_EQUAL(db.msg_store->data.retain, 0);
		CU_ASSERT_PTR_NOT_NULL(db.msg_store->data.topic);
		if(db.msg_store->data.topic){
			CU_ASSERT_STRING_EQUAL(db.msg_store->data.topic, "topic");
		}
		CU_ASSERT_EQUAL(db.msg_store->data.payloadlen, 7);
		if(db.msg_store->data.payloadlen == 7){
			CU_ASSERT_NSTRING_EQUAL(db.msg_store->data.payload, "payload", 7);
		}
	}
	test_cleanup();
}

static void TEST_v6_config_ok(void)
{
	struct mosquitto__config config;
	int rc;

	memset(&db, 0, sizeof(struct mosquitto_db));
	memset(&config, 0, sizeof(struct mosquitto__config));
	db.config = &config;

	config.persistence = true;
	char persistence_filepath[4096];
	cat_sourcedir_with_relpath(persistence_filepath, "/files/persist_read/v6-cfg.test-db");
	config.persistence_filepath = persistence_filepath;

	rc = persist__restore();
	CU_ASSERT_EQUAL(rc, MOSQ_ERR_SUCCESS);
	CU_ASSERT_EQUAL(db.last_db_id, 0x7856341200000000);
}


static void TEST_v5_config_truncated(void)
{
	struct mosquitto__config config;
	int rc;

	memset(&db, 0, sizeof(struct mosquitto_db));
	memset(&config, 0, sizeof(struct mosquitto__config));
	db.config = &config;

	config.persistence = true;
	char persistence_filepath[4096];
	cat_sourcedir_with_relpath(persistence_filepath, "/files/persist_read/v5-cfg-truncated.test-db");
	config.persistence_filepath = persistence_filepath;

	rc = persist__restore();
	CU_ASSERT_EQUAL(rc, 1);
	CU_ASSERT_EQUAL(db.last_db_id, 0);
}


static void TEST_v5_bad_chunk(void)
{
	struct mosquitto__config config;
	int rc;

	memset(&db, 0, sizeof(struct mosquitto_db));
	memset(&config, 0, sizeof(struct mosquitto__config));
	db.config = &config;

	config.persistence = true;
	char persistence_filepath[4096];
	cat_sourcedir_with_relpath(persistence_filepath, "/files/persist_read/v5-bad-chunk.test-db");
	config.persistence_filepath = persistence_filepath;

	rc = persist__restore();
	CU_ASSERT_EQUAL(rc, MOSQ_ERR_SUCCESS);
	CU_ASSERT_EQUAL(db.last_db_id, 0x17);
}


static void TEST_v6_message_store(void)
{
	struct mosquitto__config config;
	int rc;

	memset(&db, 0, sizeof(struct mosquitto_db));
	memset(&config, 0, sizeof(struct mosquitto__config));
	db.config = &config;

	config.persistence = true;
	char persistence_filepath[4096];
	cat_sourcedir_with_relpath(persistence_filepath, "/files/persist_read/v6-message-store.test-db");
	config.persistence_filepath = persistence_filepath;

	rc = persist__restore();
	CU_ASSERT_EQUAL(rc, MOSQ_ERR_SUCCESS);
	CU_ASSERT_EQUAL(db.msg_store_count, 1);
	CU_ASSERT_EQUAL(db.msg_store_bytes, 7);
	CU_ASSERT_PTR_NOT_NULL(db.msg_store);
	if(db.msg_store){
		CU_ASSERT_EQUAL(db.msg_store->data.store_id, 1);
		CU_ASSERT_STRING_EQUAL(db.msg_store->data.source_id, "source_id");
		CU_ASSERT_EQUAL(db.msg_store->data.source_mid, 2);
		CU_ASSERT_EQUAL(db.msg_store->data.qos, 2);
		CU_ASSERT_EQUAL(db.msg_store->data.retain, 1);
		CU_ASSERT_STRING_EQUAL(db.msg_store->data.topic, "topic");
		CU_ASSERT_EQUAL(db.msg_store->data.payloadlen, 7);
		if(db.msg_store->data.payloadlen == 7){
			CU_ASSERT_NSTRING_EQUAL(db.msg_store->data.payload, "payload", 7);
		}
		CU_ASSERT_PTR_NULL(db.msg_store->data.properties);
	}
	test_cleanup();
}


static void TEST_v6_message_store_props(void)
{
	struct mosquitto__config config;
	struct mosquitto__listener listener;
	int rc;

	memset(&db, 0, sizeof(struct mosquitto_db));
	memset(&config, 0, sizeof(struct mosquitto__config));
	memset(&listener, 0, sizeof(struct mosquitto__listener));
	db.config = &config;

	listener.port = 1883;
	config.listeners = &listener;
	config.listener_count = 1;

	config.persistence = true;
	char persistence_filepath[4096];
	cat_sourcedir_with_relpath(persistence_filepath, "/files/persist_read/v6-message-store-props.test-db");
	config.persistence_filepath = persistence_filepath;

	rc = persist__restore();
	CU_ASSERT_EQUAL(rc, MOSQ_ERR_SUCCESS);
	CU_ASSERT_EQUAL(db.msg_store_count, 1);
	CU_ASSERT_EQUAL(db.msg_store_bytes, 7);
	CU_ASSERT_PTR_NOT_NULL(db.msg_store);
	if(db.msg_store){
		CU_ASSERT_EQUAL(db.msg_store->data.store_id, 1);
		CU_ASSERT_STRING_EQUAL(db.msg_store->data.source_id, "source_id");
		CU_ASSERT_EQUAL(db.msg_store->data.source_mid, 2);
		CU_ASSERT_EQUAL(db.msg_store->data.qos, 2);
		CU_ASSERT_EQUAL(db.msg_store->data.retain, 1);
		CU_ASSERT_STRING_EQUAL(db.msg_store->data.topic, "topic");
		CU_ASSERT_EQUAL(db.msg_store->data.payloadlen, 7);
		if(db.msg_store->data.payloadlen == 7){
			CU_ASSERT_NSTRING_EQUAL(db.msg_store->data.payload, "payload", 7);
		}
		CU_ASSERT_PTR_NOT_NULL(db.msg_store->data.properties);
		if(db.msg_store->data.properties){
			CU_ASSERT_EQUAL(db.msg_store->data.properties->identifier, 1);
			CU_ASSERT_EQUAL(db.msg_store->data.properties->value.i8, 1);
		}
		CU_ASSERT_PTR_NOT_NULL(db.msg_store->source_listener);
	}
	test_cleanup();
}

static void TEST_v5_client(void)
{
	struct mosquitto__config config;
	struct mosquitto *context;
	int rc;

	memset(&db, 0, sizeof(struct mosquitto_db));
	memset(&config, 0, sizeof(struct mosquitto__config));
	db.config = &config;

	config.persistence = true;
	char persistence_filepath[4096];
	cat_sourcedir_with_relpath(persistence_filepath, "/files/persist_read/v5-client.test-db");
	config.persistence_filepath = persistence_filepath;

	rc = persist__restore();
	CU_ASSERT_EQUAL(rc, MOSQ_ERR_SUCCESS);

	CU_ASSERT_PTR_NOT_NULL(db.contexts_by_id);
	HASH_FIND(hh_id, db.contexts_by_id, "client-id", strlen("client-id"), context);
	CU_ASSERT_PTR_NOT_NULL(context);
	if(context){
		CU_ASSERT_PTR_NULL(context->msgs_in.inflight);
		CU_ASSERT_PTR_NULL(context->msgs_out.inflight);
		CU_ASSERT_EQUAL(context->last_mid, 0x5287);
	}
	test_cleanup();
}

static void TEST_v6_client(void)
{
	struct mosquitto__config config;
	struct mosquitto *context;
	struct mosquitto__listener listener;
	int rc;

	memset(&db, 0, sizeof(struct mosquitto_db));
	memset(&config, 0, sizeof(struct mosquitto__config));
	memset(&listener, 0, sizeof(struct mosquitto__listener));
	db.config = &config;

	listener.port = 1883;
	config.per_listener_settings = true;
	config.listeners = &listener;
	config.listener_count = 1;
	config.persistence = true;
	char persistence_filepath[4096];
	cat_sourcedir_with_relpath(persistence_filepath, "/files/persist_read/v6-client.test-db");
	config.persistence_filepath = persistence_filepath;

	rc = persist__restore();
	CU_ASSERT_EQUAL(rc, MOSQ_ERR_SUCCESS);

	CU_ASSERT_PTR_NOT_NULL(db.contexts_by_id);
	HASH_FIND(hh_id, db.contexts_by_id, "client-id", strlen("client-id"), context);
	CU_ASSERT_PTR_NOT_NULL(context);
	if(context){
		CU_ASSERT_PTR_NULL(context->msgs_in.inflight);
		CU_ASSERT_PTR_NULL(context->msgs_out.inflight);
		CU_ASSERT_EQUAL(context->last_mid, 0x5287);
		CU_ASSERT_EQUAL(context->listener, &listener);
		CU_ASSERT_PTR_NOT_NULL(context->username);
		if(context->username){
			CU_ASSERT_STRING_EQUAL(context->username, "usrname");
		}
	}
	test_cleanup();
}

static void TEST_v6_client_message(void)
{
	struct mosquitto__config config;
	struct mosquitto *context;
	int rc;

	memset(&db, 0, sizeof(struct mosquitto_db));
	memset(&config, 0, sizeof(struct mosquitto__config));
	db.config = &config;

	config.persistence = true;
	char persistence_filepath[4096];
	cat_sourcedir_with_relpath(persistence_filepath, "/files/persist_read/v6-client-message.test-db");
	config.persistence_filepath = persistence_filepath;

	rc = persist__restore();
	CU_ASSERT_EQUAL(rc, MOSQ_ERR_SUCCESS);

	CU_ASSERT_PTR_NOT_NULL(db.contexts_by_id);
	HASH_FIND(hh_id, db.contexts_by_id, "client-id", strlen("client-id"), context);
	CU_ASSERT_PTR_NOT_NULL(context);
	if(context){
		CU_ASSERT_PTR_NOT_NULL(context->msgs_out.inflight);
		if(context->msgs_out.inflight){
			CU_ASSERT_PTR_NULL(context->msgs_out.inflight->next);
			CU_ASSERT_PTR_NOT_NULL(context->msgs_out.inflight->base_msg);
			if(context->msgs_out.inflight->base_msg){
				CU_ASSERT_EQUAL(context->msgs_out.inflight->base_msg->ref_count, 1);
				CU_ASSERT_STRING_EQUAL(context->msgs_out.inflight->base_msg->data.source_id, "source_id");
				CU_ASSERT_EQUAL(context->msgs_out.inflight->base_msg->data.source_mid, 2);
				CU_ASSERT_EQUAL(context->msgs_out.inflight->base_msg->data.qos, 2);
				CU_ASSERT_EQUAL(context->msgs_out.inflight->base_msg->data.retain, 1);
				CU_ASSERT_STRING_EQUAL(context->msgs_out.inflight->base_msg->data.topic, "topic");
				CU_ASSERT_EQUAL(context->msgs_out.inflight->base_msg->data.payloadlen, 7);
				if(context->msgs_out.inflight->base_msg->data.payloadlen == 7){
					CU_ASSERT_NSTRING_EQUAL(context->msgs_out.inflight->base_msg->data.payload, "payload", 7);
				}
			}
			CU_ASSERT_EQUAL(context->msgs_out.inflight->data.mid, 0x73);
			CU_ASSERT_EQUAL(context->msgs_out.inflight->data.qos, 1);
			CU_ASSERT_EQUAL(context->msgs_out.inflight->data.retain, 0);
			CU_ASSERT_EQUAL(context->msgs_out.inflight->data.direction, mosq_md_out);
			CU_ASSERT_EQUAL(context->msgs_out.inflight->data.state, mosq_ms_wait_for_puback);
			CU_ASSERT_EQUAL(context->msgs_out.inflight->data.dup, 0);
			CU_ASSERT_EQUAL(context->msgs_out.inflight->data.subscription_identifier, 0);
		}
	}
	test_cleanup();
}

static void TEST_v6_client_message_props(void)
{
	struct mosquitto__config config;
	struct mosquitto *context;
	int rc;

	memset(&db, 0, sizeof(struct mosquitto_db));
	memset(&config, 0, sizeof(struct mosquitto__config));
	db.config = &config;

	config.persistence = true;
	char persistence_filepath[4096];
	cat_sourcedir_with_relpath(persistence_filepath, "/files/persist_read/v6-client-message-props.test-db");
	config.persistence_filepath = persistence_filepath;

	rc = persist__restore();
	CU_ASSERT_EQUAL(rc, MOSQ_ERR_SUCCESS);

	CU_ASSERT_PTR_NOT_NULL(db.contexts_by_id);
	HASH_FIND(hh_id, db.contexts_by_id, "client-id", strlen("client-id"), context);
	CU_ASSERT_PTR_NOT_NULL(context);
	if(context){
		CU_ASSERT_PTR_NOT_NULL(context->msgs_out.inflight);
		if(context->msgs_out.inflight){
			CU_ASSERT_PTR_NULL(context->msgs_out.inflight->next);
			CU_ASSERT_PTR_NOT_NULL(context->msgs_out.inflight->base_msg);
			if(context->msgs_out.inflight->base_msg){
				CU_ASSERT_EQUAL(context->msgs_out.inflight->base_msg->ref_count, 1);
				CU_ASSERT_STRING_EQUAL(context->msgs_out.inflight->base_msg->data.source_id, "source_id");
				CU_ASSERT_EQUAL(context->msgs_out.inflight->base_msg->data.source_mid, 2);
				CU_ASSERT_EQUAL(context->msgs_out.inflight->base_msg->data.qos, 2);
				CU_ASSERT_EQUAL(context->msgs_out.inflight->base_msg->data.retain, 1);
				CU_ASSERT_STRING_EQUAL(context->msgs_out.inflight->base_msg->data.topic, "topic");
				CU_ASSERT_EQUAL(context->msgs_out.inflight->base_msg->data.payloadlen, 7);
				if(context->msgs_out.inflight->base_msg->data.payloadlen == 7){
					CU_ASSERT_NSTRING_EQUAL(context->msgs_out.inflight->base_msg->data.payload, "payload", 7);
				}
			}
			CU_ASSERT_EQUAL(context->msgs_out.inflight->data.mid, 0x73);
			CU_ASSERT_EQUAL(context->msgs_out.inflight->data.qos, 1);
			CU_ASSERT_EQUAL(context->msgs_out.inflight->data.retain, 0);
			CU_ASSERT_EQUAL(context->msgs_out.inflight->data.direction, mosq_md_out);
			CU_ASSERT_EQUAL(context->msgs_out.inflight->data.state, mosq_ms_wait_for_puback);
			CU_ASSERT_EQUAL(context->msgs_out.inflight->data.dup, 0);
			CU_ASSERT_EQUAL(context->msgs_out.inflight->data.subscription_identifier, 1);
		}
	}
	test_cleanup();
}

static void TEST_v6_retain(void)
{
	struct mosquitto__config config;
	int rc;

	memset(&db, 0, sizeof(struct mosquitto_db));
	memset(&config, 0, sizeof(struct mosquitto__config));
	db.config = &config;

	config.persistence = true;
	char persistence_filepath[4096];
	cat_sourcedir_with_relpath(persistence_filepath, "/files/persist_read/v6-retain.test-db");
	config.persistence_filepath = persistence_filepath;

	retain__init();
	rc = persist__restore();
	CU_ASSERT_EQUAL(rc, MOSQ_ERR_SUCCESS);
	CU_ASSERT_EQUAL(db.msg_store_count, 1);
	CU_ASSERT_EQUAL(db.msg_store_bytes, 7);
	CU_ASSERT_PTR_NOT_NULL(db.msg_store);
	if(db.msg_store){
		CU_ASSERT_EQUAL(db.msg_store->data.store_id, 0x54);
		CU_ASSERT_STRING_EQUAL(db.msg_store->data.source_id, "source_id");
		CU_ASSERT_EQUAL(db.msg_store->data.source_mid, 2);
		CU_ASSERT_EQUAL(db.msg_store->data.qos, 2);
		CU_ASSERT_EQUAL(db.msg_store->data.retain, 1);
		CU_ASSERT_STRING_EQUAL(db.msg_store->data.topic, "topic");
		CU_ASSERT_EQUAL(db.msg_store->data.payloadlen, 7);
		if(db.msg_store->data.payloadlen == 7){
			CU_ASSERT_NSTRING_EQUAL(db.msg_store->data.payload, "payload", 7);
		}
	}
	CU_ASSERT_PTR_NOT_NULL(db.retains);
	if(db.retains){
		CU_ASSERT_STRING_EQUAL(db.retains->topic, "");
		CU_ASSERT_PTR_NOT_NULL(db.retains->children);
		if(db.retains->children){
			CU_ASSERT_STRING_EQUAL(db.retains->children->topic, "");
			CU_ASSERT_PTR_NOT_NULL(db.retains->children->children);
			if(db.retains->children->children){
				CU_ASSERT_STRING_EQUAL(db.retains->children->children->topic, "topic");
			}
		}
	}
	test_cleanup();
}

static void TEST_v6_sub(void)
{
	struct mosquitto__config config;
	struct mosquitto *context;
	int rc;

	last_sub = NULL;
	last_qos = -1;

	memset(&db, 0, sizeof(struct mosquitto_db));
	memset(&config, 0, sizeof(struct mosquitto__config));
	db.config = &config;

	config.persistence = true;
	char persistence_filepath[4096];
	cat_sourcedir_with_relpath(persistence_filepath, "/files/persist_read/v6-sub.test-db");
	config.persistence_filepath = persistence_filepath;

	rc = persist__restore();
	CU_ASSERT_EQUAL(rc, MOSQ_ERR_SUCCESS);

	CU_ASSERT_PTR_NOT_NULL(db.contexts_by_id);
	HASH_FIND(hh_id, db.contexts_by_id, "client-id", strlen("client-id"), context);
	CU_ASSERT_PTR_NOT_NULL(context);
	if(context){
		CU_ASSERT_PTR_NOT_NULL(last_sub);
		if(last_sub){
			CU_ASSERT_STRING_EQUAL(last_sub, "subscription")
			free(last_sub);
		}
		CU_ASSERT_EQUAL(last_qos, 1);
		CU_ASSERT_EQUAL(last_identifier, 0x7623);
	}
	test_cleanup();
}

/* ========================================================================
 * TEST SUITE SETUP
 * ======================================================================== */

int init_persist_read_tests(void)
{
	CU_pSuite test_suite = NULL;

	test_suite = CU_add_suite("Persist read", NULL, NULL);
	if(!test_suite){
		printf("Error adding CUnit persist read test suite.\n");
		return 1;
	}

	if(0
			|| !CU_add_test(test_suite, "Persistence disabled", TEST_persistence_disabled)
			|| !CU_add_test(test_suite, "Empty file", TEST_empty_file)
			|| !CU_add_test(test_suite, "Corrupt header", TEST_corrupt_header)
			|| !CU_add_test(test_suite, "Unsupported version", TEST_unsupported_version)
			|| !CU_add_test(test_suite, "v3 config ok", TEST_v3_config_ok)
			|| !CU_add_test(test_suite, "v3 config bad truncated", TEST_v3_config_truncated)
			|| !CU_add_test(test_suite, "v3 config bad dbid", TEST_v3_config_bad_dbid)
			|| !CU_add_test(test_suite, "v3 bad chunk", TEST_v3_bad_chunk)
			|| !CU_add_test(test_suite, "v3 message store", TEST_v3_message_store)
			|| !CU_add_test(test_suite, "v3 client", TEST_v3_client)
			|| !CU_add_test(test_suite, "v3 client message", TEST_v3_client_message)
			|| !CU_add_test(test_suite, "v3 retain", TEST_v3_retain)
			|| !CU_add_test(test_suite, "v3 sub", TEST_v3_sub)
			|| !CU_add_test(test_suite, "v4 config ok", TEST_v4_config_ok)
			|| !CU_add_test(test_suite, "v4 message store", TEST_v4_message_store)
			|| !CU_add_test(test_suite, "v5 client", TEST_v5_client)
			|| !CU_add_test(test_suite, "v5 config bad truncated", TEST_v5_config_truncated)
			|| !CU_add_test(test_suite, "v5 bad chunk", TEST_v5_bad_chunk)
			|| !CU_add_test(test_suite, "v6 config ok", TEST_v6_config_ok)
			|| !CU_add_test(test_suite, "v6 message store", TEST_v6_message_store)
			|| !CU_add_test(test_suite, "v6 message store+props", TEST_v6_message_store_props)
			|| !CU_add_test(test_suite, "v6 client", TEST_v6_client)
			|| !CU_add_test(test_suite, "v6 client message", TEST_v6_client_message)
			|| !CU_add_test(test_suite, "v6 client message+props", TEST_v6_client_message_props)
			|| !CU_add_test(test_suite, "v6 retain", TEST_v6_retain)
			|| !CU_add_test(test_suite, "v6 sub", TEST_v6_sub)
			){

		printf("Error adding persist CUnit tests.\n");
		return 1;
	}

	return 0;
}

int main(int argc, char *argv[])
{
	unsigned int fails;

	UNUSED(argc);
	UNUSED(argv);

    if(CU_initialize_registry() != CUE_SUCCESS){
        printf("Error initializing CUnit registry.\n");
        return 1;
    }

    if(0
			|| init_persist_read_tests()
			){

        CU_cleanup_registry();
        return 1;
    }

    CU_basic_set_mode(CU_BRM_VERBOSE);
    CU_basic_run_tests();
	fails = CU_get_number_of_failures();
    CU_cleanup_registry();

    return (int)fails;
}
