const { Model } = require("objection");
const uuid = require("uuid");

module.exports = class BaseStation extends Model {
  static get tableName() {
    return "basestations";
  }

  static get jsonSchema() {
    return {
      type: "object",
      properties: {
        id: { type: "string", format: "uuid" },
        name: { type: "string" },
        lat: { type: "number", default: 0 },
        lng: { type: "number", default: 0 },
      },
    };
  }

  static get relationMappings() {
    const SensorPair = require("./SensorPair");
    return {
      sensorPairs: {
        relation: Model.HasManyRelation,
        modelClass: SensorPair,
        join: {
          from: "basestations.id",
          to: "sensorpairs.basestation_id",
        },
      },
    };
  }

  async $beforeInsert() {
    this.id = uuid.v4();
  }
};
