// ============================================================================
// OSCSender.pde
// Despachador OSC multitarjet mediante oscP5 / netP5.
// Emite las 5 direcciones OSC especificadas en la arquitectura.
// ============================================================================

class OSCTarget {
  String name;
  String ip;
  int port;
  NetAddress address;

  OSCTarget(String name, String ip, int port) {
    this.name = name;
    this.ip = ip;
    this.port = port;
    this.address = new NetAddress(ip, port);
  }
}

class OSCSender {
  final String ADDR_CLOSEST_NORM   = "/installation/closest/norm";
  final String ADDR_CLOSEST_CURVE  = "/installation/closest/curve";
  final String ADDR_CLOSEST_ANGLE  = "/installation/closest/angle";
  final String ADDR_CROWD_COUNT    = "/installation/crowd/count";
  final String ADDR_CROWD_ENERGY   = "/installation/crowd/energy";

  OscP5 oscP5;
  ArrayList<OSCTarget> targets = new ArrayList<OSCTarget>();

  OSCSender(PApplet parent, JSONArray targetsArray) {
    // Puerto local de escucha efímero (12000) requerido por oscP5
    oscP5 = new OscP5(parent, 12000);

    if (targetsArray != null) {
      for (int i = 0; i < targetsArray.size(); i++) {
        JSONObject t = targetsArray.getJSONObject(i);
        String name = t.getString("name", "target_" + i);
        String ip = t.getString("ip", "127.0.0.1");
        int port = t.getInt("port", 8000);
        targets.add(new OSCTarget(name, ip, port));
        println("[INFO] Cliente OSC inicializado: " + name + " (" + ip + ":" + port + ")");
      }
    }
  }

  void sendTelemetry(
    float closestNorm,
    float closestCurve,
    float closestAngle,
    int crowdCount,
    float crowdEnergy
  ) {
    OscMessage msgNorm = new OscMessage(ADDR_CLOSEST_NORM);
    msgNorm.add(closestNorm);

    OscMessage msgCurve = new OscMessage(ADDR_CLOSEST_CURVE);
    msgCurve.add(closestCurve);

    OscMessage msgAngle = new OscMessage(ADDR_CLOSEST_ANGLE);
    msgAngle.add(closestAngle);

    OscMessage msgCount = new OscMessage(ADDR_CROWD_COUNT);
    msgCount.add(crowdCount);

    OscMessage msgEnergy = new OscMessage(ADDR_CROWD_ENERGY);
    msgEnergy.add(crowdEnergy);

    for (OSCTarget target : targets) {
      oscP5.send(msgNorm, target.address);
      oscP5.send(msgCurve, target.address);
      oscP5.send(msgAngle, target.address);
      oscP5.send(msgCount, target.address);
      oscP5.send(msgEnergy, target.address);
    }
  }
}
