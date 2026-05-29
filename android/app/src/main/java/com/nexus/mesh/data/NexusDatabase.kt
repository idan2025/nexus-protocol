package com.nexus.mesh.data

import android.content.Context
import androidx.room.Database
import androidx.room.Room
import androidx.room.RoomDatabase
import androidx.room.migration.Migration
import androidx.sqlite.db.SupportSQLiteDatabase

@Database(
    entities = [
        MessageEntity::class,
        MessageFts::class,
        ConversationEntity::class,
        ContactEntity::class,
        GroupEntity::class,
        GroupMemberEntity::class
    ],
    version = 5,
    exportSchema = false
)
abstract class NexusDatabase : RoomDatabase() {
    abstract fun messageDao(): MessageDao
    abstract fun conversationDao(): ConversationDao
    abstract fun contactDao(): ContactDao
    abstract fun groupDao(): GroupDao

    companion object {
        // Cache of open databases keyed by DB file name.
        private val instances = java.util.concurrent.ConcurrentHashMap<String, NexusDatabase>()

        private val MIGRATION_3_4 = object : Migration(3, 4) {
            override fun migrate(db: SupportSQLiteDatabase) {
                db.execSQL("ALTER TABLE contacts ADD COLUMN role INTEGER")
            }
        }

        private val MIGRATION_4_5 = object : Migration(4, 5) {
            override fun migrate(db: SupportSQLiteDatabase) {
                db.execSQL("ALTER TABLE messages ADD COLUMN reactions TEXT NOT NULL DEFAULT ''")
                db.execSQL("ALTER TABLE contacts ADD COLUMN trustLevel INTEGER NOT NULL DEFAULT 0")
            }
        }

        /**
         * Return the database for [identityTag].  When [identityTag] is blank
         * the legacy "nexus_messages.db" is used for backward compatibility.
         * Each distinct tag opens its own isolated database file so identities
         * never share conversation history.
         */
        fun getInstance(context: Context, identityTag: String = ""): NexusDatabase {
            val dbName = if (identityTag.isBlank()) "nexus_messages.db"
                         else "nexus_messages_$identityTag.db"
            return instances.getOrPut(dbName) {
                Room.databaseBuilder(
                    context.applicationContext,
                    NexusDatabase::class.java,
                    dbName
                )
                    .addMigrations(MIGRATION_3_4, MIGRATION_4_5)
                    .fallbackToDestructiveMigration()
                    .build()
            }
        }

        /** Close and evict a database when an identity is deleted. */
        fun evict(identityTag: String) {
            val dbName = if (identityTag.isBlank()) "nexus_messages.db"
                         else "nexus_messages_$identityTag.db"
            instances.remove(dbName)?.close()
        }
    }
}
