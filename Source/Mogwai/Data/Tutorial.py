def render_graph_tutorial():
    graph = RenderGraph("TutorialPass")

    graph.addPass(RenderPass("TutorialPass"), "TutorialPass")

    graph.markOutput("TutorialPass.output")

    return graph


try: m.addGraph(render_graph_tutorial())
except NameError: None
